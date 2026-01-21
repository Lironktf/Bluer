# Authentication System Documentation

## Overview

Your laundry machine tracking system now includes a complete authentication system with:
- **Optional login** - Users can use the dashboard without logging in
- **Secure password storage** - Passwords are hashed with bcrypt (one-way encryption)
- **JWT tokens** - Secure, stateless authentication
- **User rooms management** - Logged-in users can create and manage their laundry rooms

## Security Features

### Password Hashing (bcrypt)
- **Passwords are NEVER stored in plain text**
- Uses bcrypt with salt rounds of 10
- One-way hashing means passwords cannot be "decrypted"
- Industry-standard security practice

### JWT Tokens
- Tokens expire after 7 days
- Stored in HTTP-only cookies (protected from XSS)
- Include user ID, email, and username
- Verified on every authenticated request

### Data Encryption
- **Passwords**: Hashed with bcrypt (cannot be reversed)
- **Tokens**: Signed with JWT_SECRET
- **Sensitive data**: Transmitted over HTTPS only

## Database Collections

### users Collection
```javascript
{
  _id: ObjectId,
  email: String,              // Lowercase, unique
  username: String,           // Lowercase, unique
  displayName: String,        // Display name shown in UI
  password: String,           // Bcrypt hashed password
  rooms: [ObjectId],          // Array of room IDs user has access to
  createdAt: Date,
  updatedAt: Date,
  lastLogin: Date
}
```

**Indexes:**
- `email` (unique)
- `username` (unique)

### rooms Collection
```javascript
{
  _id: ObjectId,
  userId: ObjectId,           // Owner of this room
  name: String,               // Room name (e.g., "Basement Laundry")
  building: String,           // Optional building identifier
  floor: String,              // Optional floor identifier
  machineIds: [String],       // Array of machine IDs in this room
  createdAt: Date,
  updatedAt: Date
}
```

**Indexes:**
- `userId`
- `userId + createdAt` (compound, for sorted queries)

## API Endpoints

### Authentication

#### POST `/api/auth/register`
Create a new user account.

**Request:**
```json
{
  "email": "user@example.com",
  "password": "securepassword",
  "username": "johndoe"
}
```

**Response:**
```json
{
  "success": true,
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "id": "507f1f77bcf86cd799439011",
    "email": "user@example.com",
    "username": "johndoe",
    "displayName": "johndoe"
  }
}
```

**Validation:**
- Email must be valid format
- Password must be at least 6 characters
- Username must be 3-20 characters
- Email and username must be unique

#### POST `/api/auth/login`
Log in to an existing account.

**Request:**
```json
{
  "email": "user@example.com",
  "password": "securepassword"
}
```

**Response:**
```json
{
  "success": true,
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "user": {
    "id": "507f1f77bcf86cd799439011",
    "email": "user@example.com",
    "username": "johndoe",
    "displayName": "johndoe",
    "rooms": ["507f1f77bcf86cd799439012"]
  }
}
```

#### GET `/api/auth/me`
Get current authenticated user.

**Headers:**
```
Authorization: Bearer <token>
```

**Response:**
```json
{
  "success": true,
  "user": {
    "id": "507f1f77bcf86cd799439011",
    "email": "user@example.com",
    "username": "johndoe",
    "displayName": "johndoe",
    "rooms": ["507f1f77bcf86cd799439012"],
    "createdAt": "2026-01-20T...",
    "lastLogin": "2026-01-20T..."
  }
}
```

### Rooms Management

All rooms endpoints require authentication (Authorization header with Bearer token).

#### GET `/api/rooms`
Get all rooms for the current user.

**Response:**
```json
{
  "success": true,
  "rooms": [
    {
      "_id": "507f1f77bcf86cd799439012",
      "userId": "507f1f77bcf86cd799439011",
      "name": "Basement Laundry",
      "building": "Building A",
      "floor": "Basement",
      "machineIds": ["a1-m1", "a1-m2", "a1-m3"],
      "createdAt": "2026-01-20T...",
      "updatedAt": "2026-01-20T..."
    }
  ]
}
```

#### POST `/api/rooms`
Create a new room.

**Request:**
```json
{
  "name": "Basement Laundry",
  "building": "Building A",
  "floor": "Basement",
  "machineIds": ["a1-m1", "a1-m2", "a1-m3"]
}
```

**Response:**
```json
{
  "success": true,
  "room": {
    "_id": "507f1f77bcf86cd799439012",
    "userId": "507f1f77bcf86cd799439011",
    "name": "Basement Laundry",
    "building": "Building A",
    "floor": "Basement",
    "machineIds": ["a1-m1", "a1-m2", "a1-m3"],
    "createdAt": "2026-01-20T...",
    "updatedAt": "2026-01-20T..."
  }
}
```

#### PUT `/api/rooms`
Update an existing room.

**Request:**
```json
{
  "roomId": "507f1f77bcf86cd799439012",
  "name": "Updated Room Name",
  "building": "Building B",
  "floor": "1st Floor",
  "machineIds": ["a1-m1", "a1-m2"]
}
```

#### DELETE `/api/rooms?roomId=<roomId>`
Delete a room.

**Response:**
```json
{
  "success": true,
  "message": "Room deleted successfully"
}
```

## Frontend Integration

### AuthContext
The `AuthContext` provides authentication state and methods throughout the app.

**Usage:**
```jsx
import { useAuth } from '../context/AuthContext';

function MyComponent() {
  const { user, isAuthenticated, login, logout, register } = useAuth();

  if (isAuthenticated) {
    return <div>Welcome, {user.displayName}!</div>;
  }

  return <div>Please log in</div>;
}
```

**Available methods:**
- `register(email, password, username)` - Create new account
- `login(email, password)` - Log in
- `logout()` - Log out
- `user` - Current user object or null
- `isAuthenticated` - Boolean, true if logged in
- `loading` - Boolean, true while checking auth status

### Protected Content
```jsx
function ProtectedFeature() {
  const { isAuthenticated } = useAuth();

  if (!isAuthenticated) {
    return <div>Please log in to access this feature</div>;
  }

  return <div>Protected content here</div>;
}
```

## Setup Instructions

### 1. Add JWT_SECRET to Environment Variables

**For Vercel:**
1. Go to project settings
2. Add environment variable:
   - Name: `JWT_SECRET`
   - Value: Generate a secure random string (see below)
3. Deploy

**Generate secure JWT_SECRET:**
```bash
# Using OpenSSL
openssl rand -base64 32

# Using Node.js
node -e "console.log(require('crypto').randomBytes(32).toString('base64'))"
```

### 2. Update MongoDB Indexes

Add indexes for better query performance:

```javascript
// users collection
db.users.createIndex({ email: 1 }, { unique: true })
db.users.createIndex({ username: 1 }, { unique: true })

// rooms collection
db.rooms.createIndex({ userId: 1 })
db.rooms.createIndex({ userId: 1, createdAt: -1 })
```

## Security Best Practices

### ✅ What We Do
- Hash passwords with bcrypt (cannot be reversed)
- Use JWT for stateless authentication
- Validate all user input
- Use HTTPS for all communications
- Store tokens in HTTP-only cookies
- Implement proper CORS
- Use unique indexes to prevent duplicates

### ⚠️ Important Notes
- **Never share your JWT_SECRET** - Keep it secret in environment variables
- **Use HTTPS in production** - Never send auth tokens over HTTP
- **Use strong JWT_SECRET** - At least 32 random characters
- **Tokens expire** - Current setting: 7 days (configurable)
- **Login is optional** - Users can use dashboard without account

## User Flow

### Without Login
1. Visit dashboard
2. View all machine statuses
3. Use basic features
4. **Cannot** create or manage rooms

### With Login
1. Click "Login / Sign Up" in navigation
2. Create account or log in
3. Access "My Rooms" page
4. Create custom room groups
5. Filter machines by room
6. Save preferences

## Troubleshooting

### "Invalid email or password"
- Check email is correct (case-insensitive)
- Check password is correct
- User must be registered first

### "Email already registered"
- This email is already in use
- Try logging in instead
- Or use forgot password (if implemented)

### "Username already taken"
- Choose a different username
- Usernames must be unique

### "Authentication required"
- Token may be expired (7 days)
- Log in again
- Check Authorization header is set

### Token issues
- Clear cookies and log in again
- Check JWT_SECRET is set in environment
- Verify token hasn't expired

## Future Enhancements

Possible additions to the auth system:

1. **Password reset** - Email-based password recovery
2. **Email verification** - Verify email on signup
3. **OAuth** - Login with Google, GitHub, etc.
4. **2FA** - Two-factor authentication
5. **Session management** - View and revoke active sessions
6. **Room sharing** - Share rooms with other users
7. **Role-based access** - Admin, user, etc.
8. **Account settings** - Update profile, change password

## Privacy

- Passwords are never stored in plain text
- Only hashed passwords are in database
- Even admins cannot see user passwords
- User data is only accessible by that user
- Tokens are signed and verified
- All API requests use HTTPS in production

---

**Questions?** Check the main documentation or contact support!
