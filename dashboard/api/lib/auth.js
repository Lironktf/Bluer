// Authentication utilities for JWT tokens and password hashing
import bcryptjs from 'bcryptjs';
import jsonwebtoken from 'jsonwebtoken';

const bcrypt = bcryptjs;
const jwt = jsonwebtoken;

// JWT secret - should be set in environment variables
const JWT_SECRET = process.env.JWT_SECRET || 'your-secret-key-change-this-in-production';
const JWT_EXPIRES_IN = '7d'; // Token expires in 7 days

/**
 * Hash a password using bcrypt
 * @param {string} password - Plain text password
 * @returns {Promise<string>} Hashed password
 */
export async function hashPassword(password) {
  const salt = await bcrypt.genSalt(10);
  return bcrypt.hash(password, salt);
}

/**
 * Compare a password with a hash
 * @param {string} password - Plain text password
 * @param {string} hash - Hashed password from database
 * @returns {Promise<boolean>} True if password matches
 */
export async function comparePassword(password, hash) {
  return bcrypt.compare(password, hash);
}

/**
 * Generate a JWT token for a user
 * @param {object} user - User object with id and email
 * @returns {string} JWT token
 */
export function generateToken(user) {
  const payload = {
    userId: user._id.toString(),
    email: user.email,
    username: user.username
  };

  return jwt.sign(payload, JWT_SECRET, { expiresIn: JWT_EXPIRES_IN });
}

/**
 * Verify a JWT token
 * @param {string} token - JWT token to verify
 * @returns {object|null} Decoded token payload or null if invalid
 */
export function verifyToken(token) {
  try {
    return jwt.verify(token, JWT_SECRET);
  } catch (error) {
    return null;
  }
}

/**
 * Extract token from Authorization header
 * @param {object} req - Request object
 * @returns {string|null} Token or null
 */
export function extractToken(req) {
  const authHeader = req.headers.authorization;

  if (!authHeader || !authHeader.startsWith('Bearer ')) {
    return null;
  }

  return authHeader.substring(7); // Remove 'Bearer ' prefix
}

/**
 * Middleware to verify authentication
 * Returns user data if authenticated, null otherwise
 * @param {object} req - Request object
 * @returns {object|null} User data or null
 */
export function authenticateRequest(req) {
  const token = extractToken(req);

  if (!token) {
    return null;
  }

  return verifyToken(token);
}
