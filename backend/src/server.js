const express = require('express');
const cors = require('cors');
const crypto = require('crypto');
const path = require('path');
const config = require('./config');
const sessionStore = require('./sessionStore');
const razorpayService = require('./razorpayService');

const app = express();

// Capture raw body for webhook HMAC validation
app.use(express.json({
  verify: (req, res, buf) => {
    req.rawBody = buf;
  }
}));
app.use(cors());
app.use(express.static(path.join(__dirname, '../../webpage')));

// Device Authentication Middleware
function authenticateDevice(req, res, next) {
  const deviceKey = req.header('X-Device-Key');
  if (!deviceKey || deviceKey !== config.DEVICE_API_KEY) {
    console.warn(`[AUTH] Unauthorized request from IP: ${req.ip}. Invalid X-Device-Key.`);
    return res.status(401).json({ success: false, error: 'Unauthorized: Invalid Device Key' });
  }
  next();
}

// ==========================================
// 1. ESP32 API: Create QR Payment Session
// ==========================================
app.post('/api/payment/create-qr', authenticateDevice, async (req, res) => {
  try {
    const { amount, duration, deviceId, description } = req.body;

    if (!amount || isNaN(amount) || amount <= 0) {
      return res.status(400).json({ success: false, error: 'Valid amount is required' });
    }

    const durationSec = parseInt(duration) || 30;

    console.log(`[API] Creating QR for Rs. ${amount} (${durationSec}s) on device: ${deviceId}`);

    // Call Razorpay API (or Mock generator)
    const qrResult = await razorpayService.createQRCode({
      amount: parseFloat(amount),
      duration: durationSec,
      deviceId: deviceId || 'esp32_gateway',
      description
    });

    // Store session
    const session = sessionStore.createSession({
      qrId: qrResult.qr_id,
      qrPayload: qrResult.qr_payload,
      amount: qrResult.amount,
      duration: qrResult.duration,
      deviceId: deviceId || 'esp32_gateway',
      imageUrl: qrResult.image_url
    });

    return res.status(201).json({
      success: true,
      qr_id: session.qrId,
      qr_payload: session.qrPayload,
      image_url: session.imageUrl,
      amount: session.amount,
      duration: session.duration,
      expires_at: session.expiresAt
    });
  } catch (error) {
    console.error('[API] create-qr error:', error);
    return res.status(500).json({
      success: false,
      error: error.message || 'Internal server error'
    });
  }
});

// ==========================================
// 2. ESP32 API: Check Payment Verification Status (Instant Non-Blocking RAM Response)
// ==========================================
app.get('/api/payment/status/:qr_id', authenticateDevice, (req, res) => {
  try {
    const { qr_id } = req.params;
    const session = sessionStore.getSession(qr_id);

    if (!session) {
      return res.status(404).json({ success: false, error: 'Session not found or expired' });
    }

    // If still pending, trigger a non-blocking asynchronous check so the HTTP response is never stalled
    if (session.status === 'pending' && !config.MOCK_MODE && sessionStore.shouldCheckRemote(session)) {
      razorpayService.checkQRPayments(qr_id, session.amount, session.deviceId, session.createdAt).then(livePayment => {
        if (livePayment && (livePayment.status === 'captured' || livePayment.status === 'authorized')) {
          console.log(`[API] Live payment verified via Razorpay API: ${livePayment.id} for session ${qr_id}`);
          sessionStore.markCaptured(qr_id, livePayment.id);
        }
      }).catch(err => {
        console.error(`[API] Async check error for ${qr_id}:`, err.message);
      });
    }

    // Instant response from RAM (sub-millisecond latency for ESP32)
    return res.json({
      success: true,
      qr_id: session.qrId,
      status: session.status, // 'pending', 'captured', 'consumed', 'expired'
      amount: session.amount,
      duration: session.duration,
      payment_id: session.paymentId || '',
      activation_token: session.activationToken || ''
    });
  } catch (error) {
    console.error('[API] status check error:', error);
    return res.status(500).json({ success: false, error: error.message });
  }
});

// ==========================================
// 3. ESP32 API: Consume Authorized Payment Token
// ==========================================
app.post('/api/payment/consume/:qr_id', authenticateDevice, (req, res) => {
  const { qr_id } = req.params;
  const { paymentId, token } = req.body;

  const result = sessionStore.markConsumed(qr_id, paymentId, token);
  if (!result.success) {
    return res.status(400).json(result);
  }

  return res.json({
    success: true,
    message: 'Payment session marked consumed. Relay activation approved.'
  });
});

// ==========================================
// 3b. ESP32 API: Cancel Pending Payment Session & Close on Razorpay
// ==========================================
app.post('/api/payment/cancel/:qr_id', authenticateDevice, (req, res) => {
  try {
    const { qr_id } = req.params;
    const session = sessionStore.getSession(qr_id);

    if (!session) {
      return res.status(404).json({ success: false, error: 'Session not found' });
    }

    sessionStore.markCancelled(qr_id);

    if (!config.MOCK_MODE) {
      razorpayService.closeQRCode(qr_id).catch(err => {
        console.error(`[API] Async QR close failed for ${qr_id}:`, err.message);
      });
    }

    return res.json({
      success: true,
      message: 'Payment session cancelled and closed on Razorpay.'
    });
  } catch (error) {
    console.error('[API] Cancel error:', error);
    return res.status(500).json({ success: false, error: error.message });
  }
});

// ==========================================
// 4. Razorpay Webhook Verification Endpoint
// ==========================================
app.post('/api/payment/webhook', (req, res) => {
  try {
    const signature = req.headers['x-razorpay-signature'];
    if (!signature) {
      console.warn('[WEBHOOK] Missing x-razorpay-signature header.');
      return res.status(400).send('Missing signature');
    }

    if (!config.RAZORPAY_WEBHOOK_SECRET) {
      console.warn('[WEBHOOK] RAZORPAY_WEBHOOK_SECRET is not configured in .env!');
      return res.status(500).send('Webhook secret unconfigured');
    }

    // Authenticate signature using HMAC-SHA256
    const expectedSignature = crypto
      .createHmac('sha256', config.RAZORPAY_WEBHOOK_SECRET)
      .update(req.rawBody || '')
      .digest('hex');

    const sigBuf = Buffer.from(String(signature));
    const expectedBuf = Buffer.from(expectedSignature);
    if (sigBuf.length !== expectedBuf.length || !crypto.timingSafeEqual(sigBuf, expectedBuf)) {
      console.warn('[WEBHOOK] Invalid webhook signature!');
      return res.status(400).send('Invalid signature');
    }

    const event = req.body;
    console.log(`[WEBHOOK] Verified Razorpay event: ${event.event}`);

    // Handle payment capture / QR credit events
    if (event.event === 'payment.captured' || event.event === 'qr_code.credited') {
      const payload = event.payload;
      let qrId = null;
      let paymentId = null;

      if (payload.qr_code && payload.qr_code.entity) {
        qrId = payload.qr_code.entity.id;
      }
      if (payload.payment && payload.payment.entity) {
        paymentId = payload.payment.entity.id;
        // Sometimes qr_id is present in payment notes or description
        if (!qrId && payload.payment.entity.notes && payload.payment.entity.notes.qr_id) {
          qrId = payload.payment.entity.notes.qr_id;
        }
        // Match pending session by deviceId from notes if qrId is not directly attached
        if (!qrId && payload.payment.entity.notes && payload.payment.entity.notes.deviceId) {
          const matched = sessionStore.findPendingSessionByDevice(payload.payment.entity.notes.deviceId);
          if (matched) {
            qrId = matched.qrId;
          }
        }
      }

      if (qrId && paymentId) {
        sessionStore.markCaptured(qrId, paymentId);
      }
    }

    return res.status(200).json({ status: 'ok' });
  } catch (error) {
    console.error('[WEBHOOK] Processing error:', error);
    return res.status(500).send('Error');
  }
});

// ==========================================
// 5. Development / Mock Mode Simulator Endpoint
// ==========================================
function requireSimulation(req, res, next) {
  if (!config.ALLOW_SIMULATION) {
    return res.status(404).send('Not found');
  }
  next();
}

app.post('/api/mock/pay/:qr_id', requireSimulation, (req, res) => {
  const { qr_id } = req.params;
  const session = sessionStore.getSession(qr_id);

  if (!session) {
    return res.status(404).json({ success: false, error: 'Session not found' });
  }
  if (session.status !== 'pending') {
    return res.status(400).json({ success: false, error: `Session is ${session.status}` });
  }

  const mockPaymentId = 'pay_sim_' + Math.random().toString(36).substring(2, 10);
  sessionStore.markCaptured(qr_id, mockPaymentId);

  return res.json({
    success: true,
    message: 'Simulated payment captured!',
    qr_id,
    payment_id: mockPaymentId
  });
});

// Phone-friendly test page: open http://<backend-ip>:<port>/test, scan the QR, then tap "Mark Paid"
app.get('/test', requireSimulation, (req, res) => {
  const pending = sessionStore.getAllSessions().filter(s => sessionStore.getSession(s.qrId).status === 'pending');
  const rows = pending.map(s => `
    <div class="card">
      <img src="${s.imageUrl}" alt="QR">
      <p><b>Rs. ${s.amount}</b> &middot; ${s.duration}s &middot; ${s.deviceId}<br><small>${s.qrId}</small></p>
      <button onclick="pay('${s.qrId}')">Mark Paid</button>
    </div>`).join('') || '<p>No pending payments. Trigger one from the ESP32.</p>';

  res.send(`<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Gateway Test</title><meta http-equiv="refresh" content="5">
<style>body{font-family:sans-serif;max-width:420px;margin:auto;padding:12px}
.card{border:1px solid #ccc;border-radius:8px;padding:12px;margin:12px 0;text-align:center}
img{width:220px}button{font-size:18px;padding:10px 24px}</style></head><body>
<h2>Pending Payments</h2><p>Mode: ${config.MOCK_MODE ? 'MOCK' : 'RAZORPAY TEST'}</p>${rows}
<script>async function pay(id){const r=await fetch('/api/mock/pay/'+id,{method:'POST'});
alert((await r.json()).message||'Failed');location.reload();}</script></body></html>`);
});

// ==========================================
// 6. Health & Status Check
// ==========================================
// Web Order Creation (Razorpay Standard Orders)
app.post('/api/order/create', async (req, res) => {
  try {
    const { amount, currency, notes } = req.body;
    const amountInPaise = Math.round(parseFloat(amount) * 100);

    if (!config.MOCK_MODE && razorpayService.instance) {
      const order = await razorpayService.instance.orders.create({
        amount: amountInPaise,
        currency: currency || 'INR',
        receipt: `rcpt_${Date.now()}`,
        notes: notes || {}
      });
      return res.json({ success: true, order, keyId: config.RAZORPAY_KEY_ID });
    } else {
      const mockOrderId = 'order_mock_' + Math.random().toString(36).substring(2, 10);
      return res.json({
        success: true,
        order: {
          id: mockOrderId,
          amount: amountInPaise,
          currency: 'INR',
          status: 'created'
        },
        keyId: config.RAZORPAY_KEY_ID || 'rzp_test_placeholder'
      });
    }
  } catch (err) {
    console.error('[API] /api/order/create error:', err);
    return res.status(500).json({ success: false, error: err.message });
  }
});

app.get('/api/health', (req, res) => {
  res.json({
    status: 'online',
    mockMode: config.MOCK_MODE,
    activeSessions: sessionStore.getAllSessions().length,
    timestamp: new Date().toISOString()
  });
});

// ==========================================
// Proactive High-Speed Payment Poller
// Actively polls Razorpay for pending sessions so captures are detected as fast as possible
// without blocking any ESP32 HTTP requests
// ==========================================
let pollerActive = false;
async function pollPendingSessions() {
  if (pollerActive || config.MOCK_MODE) return;
  const pending = sessionStore.getPendingSessions();
  if (pending.length === 0) return;

  pollerActive = true;
  try {
    const batch = pending.slice(0, 6);
    for (const session of batch) {
      if (sessionStore.shouldCheckRemote(session)) {
        const livePayment = await razorpayService.checkQRPayments(session.qrId, session.amount, session.deviceId, session.createdAt);
        if (livePayment && (livePayment.status === 'captured' || livePayment.status === 'authorized')) {
          console.log(`[POLLER] Fast payment detected via Razorpay API: ${livePayment.id} for session ${session.qrId}`);
          sessionStore.markCaptured(session.qrId, livePayment.id);
        }
      }
    }
  } catch (err) {
    console.error('[POLLER] Verification error:', err.message);
  } finally {
    pollerActive = false;
  }
}
setInterval(pollPendingSessions, 500);

// Periodic session cleanup
setInterval(() => {
  sessionStore.cleanup();
}, 60000);

// Start Server
app.listen(config.PORT, () => {
  console.log('====================================================');
  console.log(` SMART PAYMENT GATEWAY BACKEND LISTENING ON PORT ${config.PORT}`);
  console.log(` Mode: ${config.MOCK_MODE ? 'SANDBOX / MOCK' : 'LIVE RAZORPAY API'}`);
  console.log(` Device Key: ${config.DEVICE_API_KEY}`);
  if (config.ALLOW_SIMULATION) {
    console.log(` Test page: http://<this-pc-ip>:${config.PORT}/test`);
  }
  if (config.MOCK_MODE && !config.MERCHANT_VPA) {
    console.log(' WARNING: MERCHANT_VPA not set - UPI apps will reject the QR.');
  }
  console.log('====================================================');
});

