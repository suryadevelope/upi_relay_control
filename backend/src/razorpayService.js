const https = require('https');
const Razorpay = require('razorpay');
const jsQR = require('jsqr');
const { Jimp } = require('jimp');
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
        note: `SmartGateway ${duration}s`
      });
      
      return {
        qr_id: mockQrId,
        qr_payload: mockUpiPayload,
        image_url: `https://api.qrserver.com/v1/create-qr-code/?size=300x300&data=${encodeURIComponent(mockUpiPayload)}`,
        amount,
        duration
      };
    }

    try {
      const amountInPaise = Math.round(amount * 100);
      const options = {
        type: 'upi_qr',
        name: 'Smart Gateway',
        usage: 'single_use',
        fixed_amount: true,
        payment_amount: amountInPaise,
        // Close on Razorpay's side too, so a stale QR can't be paid after the session expires
        close_by: Math.floor(Date.now() / 1000) + config.QR_EXPIRY_SEC,
        description: description || `Access for ${duration}s`,
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
      // Eliminates external S3 image downloading and CPU-heavy Jimp decoding,
      // enabling 15-30 concurrent devices to generate QR codes with sub-second response times.
      const trVal = `${response.id.replace(/^qr_/, '')}qrv2`;
      const vpa = config.MERCHANT_VPA || 'funnhub151684.rzp@rxairtel';
      const name = config.MERCHANT_NAME || 'FUNNHUB';
      const note = `Payment To ${name}`;
      const qrPayload = `upi://pay?am=${amount.toFixed(2)}&cu=INR&mc=5811&mode=22&pa=${vpa}&pn=${encodeURIComponent(name)}&tn=${encodeURIComponent(note)}&tr=${trVal}`;
      console.log(`[RAZORPAY] Fast UPI payload generated: ${qrPayload}`);

      return {
        qr_id: response.id,
        qr_payload: qrPayload,
        image_url: response.image_url,
        amount,
        duration
      };
    } catch (error) {
      console.error('[RAZORPAY] Failed to create QR Code via API:', error);
      throw error;
    }
  }

  // Fetch payment records associated with QR Code
  async checkQRPayments(qrId, expectedAmount) {
    if (config.MOCK_MODE) {
      return null;
    }

    try {
      const payments = await this.instance.qrCode.fetchAllPayments(qrId);
      if (!payments || !payments.items || payments.items.length === 0) {
        return null;
      }

      const expectedPaise = Math.round(expectedAmount * 100);
      // Find captured payment matching expected amount
      const validPayment = payments.items.find(
        p => p.status === 'captured' && p.amount === expectedPaise
      );

      return validPayment || null;
    } catch (error) {
      console.error(`[RAZORPAY] Error fetching payments for QR ${qrId}:`, error);
      return null;
    }
  }
}

module.exports = new RazorpayService();

