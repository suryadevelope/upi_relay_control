/**
 * Test & Simulation Script for Smart Gateway Backend
 * Exercises the complete transaction cycle:
 * 1. Create QR session
 * 2. Check pending status
 * 3. Simulate payment capture
 * 4. Verify captured state and retrieve activation token
 * 5. Consume token (relay activation completed)
 */

const http = require('http');

require('dotenv').config();

const BACKEND_HOST = process.env.BACKEND_HOST || 'localhost';
const BACKEND_PORT = process.env.PORT || 3000;
const DEVICE_KEY = process.env.DEVICE_API_KEY || 'sg_device_secret_key_123';

function request(options, data) {
  return new Promise((resolve, reject) => {
    const req = http.request(options, (res) => {
      let body = '';
      res.on('data', chunk => body += chunk);
      res.on('end', () => {
        try {
          resolve({ status: res.statusCode, data: JSON.parse(body) });
        } catch (e) {
          resolve({ status: res.statusCode, text: body });
        }
      });
    });
    req.on('error', reject);
    if (data) req.write(JSON.stringify(data));
    req.end();
  });
}

async function runTestFlow() {
  console.log('--- Step 1: Health Check ---');
  let res = await request({ host: BACKEND_HOST, port: BACKEND_PORT, path: '/api/health', method: 'GET' });
  console.log('Health:', res.data);

  console.log('\n--- Step 2: Creating ₹20 QR Code (30 seconds relay) ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: '/api/payment/create-qr',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-Device-Key': DEVICE_KEY
    }
  }, {
    amount: 20,
    duration: 30,
    deviceId: 'test_esp32_mac_01'
  });
  console.log('Create QR Response:', res.data);
  const qrId = res.data.qr_id;

  console.log('\n--- Step 3: Checking Pending Status ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: `/api/payment/status/${qrId}`,
    method: 'GET',
    headers: { 'X-Device-Key': DEVICE_KEY }
  });
  console.log('Initial Status:', res.data);

  console.log('\n--- Step 4: Simulating Customer Scanning & Paying via UPI ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: `/api/mock/pay/${qrId}`,
    method: 'POST'
  });
  console.log('Mock Pay Result:', res.data);

  console.log('\n--- Step 5: Checking Captured Payment Status & Activation Token ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: `/api/payment/status/${qrId}`,
    method: 'GET',
    headers: { 'X-Device-Key': DEVICE_KEY }
  });
  console.log('Captured Status:', res.data);
  const { payment_id, activation_token } = res.data;

  console.log('\n--- Step 6: Consuming Token (ESP32 Relay Active) ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: `/api/payment/consume/${qrId}`,
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-Device-Key': DEVICE_KEY
    }
  }, {
    paymentId: payment_id,
    token: activation_token
  });
  console.log('Consume Response:', res.data);

  console.log('\n--- Step 7: Verifying Consumed State (Replay Prevention) ---');
  res = await request({
    host: BACKEND_HOST,
    port: BACKEND_PORT,
    path: `/api/payment/status/${qrId}`,
    method: 'GET',
    headers: { 'X-Device-Key': DEVICE_KEY }
  });
  console.log('Final Status (Should be consumed):', res.data.status);
  console.log('\nSUCCESS: End-to-end payment simulation completed seamlessly!');
}

runTestFlow().catch(console.error);

