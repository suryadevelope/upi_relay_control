const crypto = require('crypto');
const config = require('./config');

function safeEqual(a, b) {
  const bufA = Buffer.from(String(a || ''));
  const bufB = Buffer.from(String(b || ''));
  return bufA.length === bufB.length && crypto.timingSafeEqual(bufA, bufB);
}

class SessionStore {
  constructor() {
    this.sessions = new Map();
  }

  createSession({ qrId, qrPayload, amount, duration, deviceId, imageUrl }) {
    const session = {
      qrId,
      qrPayload,
      amount,
      duration,
      deviceId,
      imageUrl: imageUrl || '',
      status: 'pending', // 'pending', 'captured', 'consumed', 'expired'
      paymentId: null,
      activationToken: null,
      lastRemoteCheck: 0,
      createdAt: Date.now(),
      expiresAt: Date.now() + config.QR_EXPIRY_SEC * 1000
    };
    this.sessions.set(qrId, session);
    return session;
  }

  getSession(qrId) {
    const session = this.sessions.get(qrId);
    if (!session) return null;

    // Check expiration
    if (session.status === 'pending' && Date.now() > session.expiresAt) {
      session.status = 'expired';
    }
    return session;
  }

  // Get all active pending sessions for proactive background verification
  getPendingSessions() {
    const now = Date.now();
    const pending = [];
    for (const session of this.sessions.values()) {
      if (session.status === 'pending') {
        if (now > session.expiresAt) {
          session.status = 'expired';
        } else {
          pending.push(session);
        }
      }
    }
    return pending;
  }

  markCaptured(qrId, paymentId) {
    const session = this.getSession(qrId);
    if (!session) return null;

    if (session.status === 'consumed') {
      console.warn(`[STORE] Attempted capture on already consumed session: ${qrId}`);
      return session;
    }

    session.status = 'captured';
    session.paymentId = paymentId;
    // Generate secure cryptographic activation token for ESP32
    session.activationToken = crypto.randomBytes(24).toString('hex');
    console.log(`[STORE] Session ${qrId} marked CAPTURED with payment ID: ${paymentId}`);
    return session;
  }

  markConsumed(qrId, paymentId, token) {
    const session = this.getSession(qrId);
    if (!session) return { success: false, error: 'Session not found' };

    if (session.status !== 'captured') {
      return { success: false, error: `Invalid state for consumption: ${session.status}` };
    }

    if (!safeEqual(session.activationToken, token)) {
      return { success: false, error: 'Invalid activation token' };
    }

    session.status = 'consumed';
    console.log(`[STORE] Session ${qrId} marked permanently CONSUMED by device.`);
    return { success: true, session };
  }

  // Throttle remote (Razorpay) status checks; returns true if a check is due now
  shouldCheckRemote(session) {
    const now = Date.now();
    if (now - session.lastRemoteCheck < config.STATUS_POLL_MIN_MS) return false;
    session.lastRemoteCheck = now;
    return true;
  }

  getAllSessions() {
    return Array.from(this.sessions.values());
  }

  // Find active pending session by deviceId (used by webhooks when qrId isn't explicitly in payload)
  findPendingSessionByDevice(deviceId) {
    if (!deviceId) return null;
    for (const session of this.sessions.values()) {
      if (session.status === 'pending' && session.deviceId === deviceId) {
        return session;
      }
    }
    return null;
  }

  // Periodic cleanup of sessions older than 30 mins
  cleanup() {
    const cutoff = Date.now() - 30 * 60 * 1000;
    for (const [id, s] of this.sessions.entries()) {
      if (s.createdAt < cutoff) {
        this.sessions.delete(id);
      }
    }
  }
}

module.exports = new SessionStore();

