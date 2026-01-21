// User registration endpoint
import { getCollection } from '../lib/mongodb.js';
import { hashPassword, generateToken } from '../lib/auth.js';

export default async function handler(req, res) {
  // Enable CORS
  res.setHeader('Access-Control-Allow-Credentials', true);
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'POST,OPTIONS');
  res.setHeader(
    'Access-Control-Allow-Headers',
    'X-CSRF-Token, X-Requested-With, Accept, Accept-Version, Content-Length, Content-MD5, Content-Type, Date, X-Api-Version'
  );

  if (req.method === 'OPTIONS') {
    res.status(200).end();
    return;
  }

  if (req.method !== 'POST') {
    return res.status(405).json({ error: 'Method not allowed' });
  }

  try {
    console.log('📝 Registration attempt started');
    const { email, password, username } = req.body;

    // Validate input
    if (!email || !password || !username) {
      console.log('❌ Missing required fields');
      return res.status(400).json({
        success: false,
        error: 'Email, username, and password are required'
      });
    }

    console.log(`📝 Registering user: ${username} (${email})`);


    // Validate email format
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) {
      return res.status(400).json({
        success: false,
        error: 'Invalid email format'
      });
    }

    // Validate password length
    if (password.length < 6) {
      return res.status(400).json({
        success: false,
        error: 'Password must be at least 6 characters long'
      });
    }

    // Validate username
    if (username.length < 3 || username.length > 20) {
      return res.status(400).json({
        success: false,
        error: 'Username must be between 3 and 20 characters'
      });
    }

    console.log('🔗 Connecting to database...');
    const users = await getCollection('users');
    console.log('✅ Database connected');

    // Check if user already exists
    console.log('🔍 Checking for existing user...');
    const existingUser = await users.findOne({
      $or: [{ email: email.toLowerCase() }, { username: username.toLowerCase() }]
    });

    if (existingUser) {
      console.log('❌ User already exists');
      return res.status(409).json({
        success: false,
        error: existingUser.email === email.toLowerCase()
          ? 'Email already registered'
          : 'Username already taken'
      });
    }

    // Hash password
    console.log('🔐 Hashing password...');
    const hashedPassword = await hashPassword(password);
    console.log('✅ Password hashed');

    // Create user document
    const newUser = {
      email: email.toLowerCase(),
      username: username.toLowerCase(),
      displayName: username,
      password: hashedPassword,
      rooms: [], // Array of room IDs this user has access to
      createdAt: new Date(),
      updatedAt: new Date()
    };

    console.log('💾 Inserting user into database...');
    const result = await users.insertOne(newUser);
    console.log('✅ User inserted with ID:', result.insertedId);

    // Generate JWT token
    console.log('🎫 Generating JWT token...');
    const token = generateToken({
      _id: result.insertedId,
      email: newUser.email,
      username: newUser.username
    });

    console.log(`✅ New user registered: ${username} (${email})`);

    return res.status(201).json({
      success: true,
      token,
      user: {
        id: result.insertedId,
        email: newUser.email,
        username: newUser.username,
        displayName: newUser.displayName
      }
    });

  } catch (error) {
    console.error('❌ Registration error:', error);
    console.error('Error stack:', error.stack);
    return res.status(500).json({
      success: false,
      error: 'Internal server error',
      message: process.env.NODE_ENV === 'development' ? error.message : 'Registration failed'
    });
  }
}
