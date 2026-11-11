require('dotenv').config();

module.exports = {
  PORT: process.env.PORT || 3000,
  RAZORPAY_KEY_ID: process.env.RAZORPAY_KEY_ID || '',
  RAZORPAY_KEY_SECRET: process.env.RAZORPAY_KEY_SECRET || '',
  RAZORPAY_WEBHOOK_SECRET: process.env.RAZORPAY_WEBHOOK_SECRET || '',
  // Real UPI ID used for the QR in mock mode (e.g. yourname@okaxis). Fake IDs are rejected by UPI apps.
  MERCHANT_VPA: process.env.MERCHANT_VPA || '',
  MERCHANT_NAME: process.env.MERCHANT_NAME || 'Smart Gateway',
  // QR / session validity in seconds (Razorpay requires close_by >= 2 minutes ahead)
  QR_EXPIRY_SEC: Math.max(parseInt(process.env.QR_EXPIRY_SEC) || 300, 120),
  // Minimum gap between Razorpay status API calls per session (avoids rate limits from ESP32 polling)
  // Min gap between Razorpay status checks per session. 1 s keeps payment confirmation fast.
  STATUS_POLL_MIN_MS: parseInt(process.env.STATUS_POLL_MIN_MS) || 1000,
  DEVICE_API_KEY: process.env.DEVICE_API_KEY || 'sg_device_secret_key_123',
  MOCK_MODE: process.env.MOCK_MODE === 'true' || !process.env.RAZORPAY_KEY_ID || process.env.RAZORPAY_KEY_ID.includes('YourKey')
};

// Payment simulation (test page + /api/mock/pay) is only allowed when no real money can be involved
module.exports.ALLOW_SIMULATION = module.exports.MOCK_MODE || module.exports.RAZORPAY_KEY_ID.startsWith('rzp_test_');

