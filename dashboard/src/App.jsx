import { Routes, Route } from 'react-router-dom';
import { Analytics } from '@vercel/analytics/react';
import { AuthProvider } from './context/AuthContext';
import Navigation from './components/Navigation/Navigation';
import Footer from './components/Footer/Footer';
import Dashboard from './pages/Dashboard';
import About from './pages/About';
import MyRooms from './pages/MyRooms';

function App() {
  return (
    <AuthProvider>
      <Navigation />
      <Routes>
        <Route path="/" element={<Dashboard />} />
        <Route path="/about" element={<About />} />
        <Route path="/my-rooms" element={<MyRooms />} />
      </Routes>

      <Footer />

      {/* Cookieless page-view tracking. Only runs on Vercel; a local dev build
          logs to the console instead of sending anything. Requires Web
          Analytics to be switched on in the Vercel project settings. */}
      <Analytics />
    </AuthProvider>
  );
}

export default App;
