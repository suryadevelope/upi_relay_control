const https = require('https');
const Razorpay = require('razorpay');
const QRCode = require('qrcode');
const config = require('./config');

// Keep-Alive HTTPS Agent to eliminate repeated TLS handshake latency to api.razorpay.com
const razorpayHttpsAgent = new https.Agent({
  keepAlive: true,
  maxSockets: 50,
  maxFreeSockets: 10,
  timeout: 30000
});

// Build a UPI intent string. Values must be URL-encoded, and `pa` must be a real VPA
// or UPI apps reject the QR with "beneficiary UPI ID incorrect / invalid".
function buildUpiPayload({ vpa, name, amount, note }) {
  const params = new URLSearchParams({
    pa: vpa,
    pn: name,
    am: amount.toFixed(2),
    cu: 'INR',
    tn: note
  });
  // Keep '@' literal in the VPA: some UPI apps don't decode %40 and reject the beneficiary.
  return `upi://pay?${params.toString().replace(/\+/g, '%20').replace(/%40/g, '@')}`;
}

// Razorpay only returns a QR *image* URL, not the raw UPI string. Download the image
// and decode it so the ESP32 renders the real upi:// payload (with Razorpay's VPA).
async function decodeQrFromImageUrl(imageUrl) {
  const res = await fetch(imageUrl, { redirect: 'follow', signal: AbortSignal.timeout(3500) });
  if (!res.ok) {
    throw new Error(`Failed to download QR image (${res.status})`);
  }
  const image = await Jimp.read(Buffer.from(await res.arrayBuffer()));
  const { data, width, height } = image.bitmap;
  const result = jsQR(new Uint8ClampedArray(data), width, height);
  if (!result || !result.data.toLowerCase().startsWith('upi://')) {
    throw new Error('Could not extract UPI payload from Razorpay QR image');
  }
  return result.data;
}

class RazorpayService {
  constructor() {
    this.instance = null;
    if (!config.MOCK_MODE) {
      this.instance = new Razorpay({
        key_id: config.RAZORPAY_KEY_ID,
        key_secret: config.RAZORPAY_KEY_SECRET
      });
      if (this.instance.api && this.instance.api.rq) {
        this.instance.api.rq.defaults.httpsAgent = razorpayHttpsAgent;
      }
      console.log('[RAZORPAY] Initialized with Live API credentials and HTTPS Keep-Alive.');
    } else {
      console.log('[RAZORPAY] Operating in MOCK / SANDBOX mode.');
    }
  }

  // Create single-use fixed amount UPI QR Code
  async createQRCode({ amount, duration, deviceId, description }) {
    if (config.MOCK_MODE) {
      const mockQrId = 'qr_mock_' + Math.random().toString(36).substring(2, 12);
      if (!config.MERCHANT_VPA) {
        console.warn('[RAZORPAY] MERCHANT_VPA not set in .env - mock QR will be rejected by UPI apps (invalid beneficiary).');
      }
      // Standard UPI intent payload recognized by all UPI apps (GPay, PhonePe, Paytm, BHIM)
      const mockUpiPayload = buildUpiPayload({
        vpa: config.MERCHANT_VPA || 'not-configured@upi',
        name: config.MERCHANT_NAME,
        amount,
        note: `Game Play ${duration}s`
      });

      // Generate local offline base64 QR Data URL with qrcode npm library (zero external API dependency)
      const mockQrDataUrl = await QRCode.toDataURL(mockUpiPayload, { width: 300, margin: 1 });
      
      return {
        qr_id: mockQrId,
        qr_payload: mockUpiPayload,
        image_url: mockQrDataUrl,
        amount,
        duration
      };
    }

    try {
      const amountInPaise = Math.round(amount * 100);
      const options = {
        type: 'upi_qr',
        name: 'FUNNHUB ARCADE',
        usage: 'single_use',
        fixed_amount: true,
        payment_amount: amountInPaise,
        // Close on Razorpay's side too, so a stale QR can't be paid after the session expires
        close_by: Math.floor(Date.now() / 1000) + config.QR_EXPIRY_SEC,
        description: description || `Game Play for ${duration}s`,
        notes: {
          deviceId: deviceId || 'esp32',
          duration: String(duration)
        }
      };

      console.log('[RAZORPAY] Calling API POST /v1/payments/qr_codes with:', options);
      const t0 = Date.now();
      const response = await this.instance.qrCode.create(options);
      console.log(`[RAZORPAY] QR Code created: ${response.id} (${Date.now() - t0} ms)`);

      // Fast deterministic UPI intent construction (verified with live Razorpay payments).
      const trVal = `${response.id.replace(/^qr_/, '')}qrv2`;
      const vpa = config.MERCHANT_VPA || 'funnhub151684.rzp@rxairtel';
      const name = config.MERCHANT_NAME || 'FUNNHUB';
      const note = `Payment To ${name}`;
      const qrPayload = `upi://pay?am=${amount.toFixed(2)}&cu=INR&mc=5811&mode=22&pa=${vpa}&pn=${encodeURIComponent(name)}&tn=${encodeURIComponent(note)}&tr=${trVal}`;
      console.log(`[RAZORPAY] Fast UPI payload generated: ${qrPayload}`);

      // Generate self-hosted local offline QR code image DataURL using npm qrcode library
      let qrImageUrl = '';
      try {
        qrImageUrl = await QRCode.toDataURL(qrPayload, { width: 300, margin: 1 });
      } catch (qrErr) {
        qrImageUrl = response.image_url;
      }

      return {
        qr_id: response.id,
        qr_payload: qrPayload,
        image_url: qrImageUrl,
        amount,
        duration
      };
    } catch (error) {
      console.error('[RAZORPAY] Failed to create QR Code via API:', error);
      throw error;
    }
  }

  // Fetch payment records associated with QR Code or Device
  // Solves the 15-30 second Razorpay QR indexing delay by also checking the real-time payments ledger!
  async checkQRPayments(qrId, expectedAmount, deviceId, sessionCreatedAt) {
    if (config.MOCK_MODE) {
      return null;
    }

    try {
      const expectedPaise = Math.round(expectedAmount * 100);

      // 1. Direct check on QR entity
      const payments = await this.instance.qrCode.fetchAllPayments(qrId);
      if (payments && payments.items && payments.items.length > 0) {
        const validPayment = payments.items.find(
          p => (p.status === 'captured' || p.status === 'authorized') && p.amount === expectedPaise
        );
        if (validPayment) return validPayment;
      }

      // 2. Real-Time Account Ledger Check (Eliminates the 15-30s Razorpay QR indexing delay!)
      // When a customer pays via UPI, Razorpay adds the payment to payments.all instantly (< 1s).
      const recent = await this.instance.payments.all({ count: 10 });
      if (recent && recent.items && recent.items.length > 0) {
        const minTimeSec = sessionCreatedAt ? Math.floor(sessionCreatedAt / 1000) - 10 : 0;
        const matched = recent.items.find(p => {
          const matchDevice = deviceId ? (p.notes && p.notes.deviceId === deviceId) : true;
          const matchAmount = p.amount === expectedPaise;
          const matchStatus = p.status === 'captured' || p.status === 'authorized';
          const matchTime = p.created_at >= minTimeSec;
          return matchDevice && matchAmount && matchStatus && matchTime;
        });

        if (matched) {
          console.log(`[RAZORPAY] Instant real-time payment match in payments.all: ${matched.id} for device ${deviceId}`);
          return matched;
        }
      }

      return null;
    } catch (error) {
      console.error(`[RAZORPAY] Error fetching payments for QR ${qrId}:`, error.message);
      return null;
    }
  }

  // Close / invalidate QR code on Razorpay servers so no late payment can be accepted
  async closeQRCode(qrId) {
    if (config.MOCK_MODE || !qrId || qrId.startsWith('qr_mock_')) {
      return true;
    }

    try {
      console.log(`[RAZORPAY] Closing QR code ${qrId} on Razorpay...`);
      const response = await this.instance.qrCode.close(qrId);
      console.log(`[RAZORPAY] QR code ${qrId} closed successfully (status: ${response.status})`);
      return true;
    } catch (error) {
      console.error(`[RAZORPAY] Error closing QR code ${qrId}:`, error.message);
      return false;
    }
  }
}

module.exports = new RazorpayService();

