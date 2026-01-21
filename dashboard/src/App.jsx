import { Routes, Route } from 'react-router-dom';
import { AuthProvider } from './context/AuthContext';
import Dashboard from './pages/Dashboard';
import About from './pages/About';
import MyRooms from './pages/MyRooms';

function App() {
  return (
    <AuthProvider>
      <Routes>
        <Route path="/" element={<Dashboard />} />
        <Route path="/about" element={<About />} />
        <Route path="/my-rooms" element={<MyRooms />} />
      </Routes>
    </AuthProvider>
  );
}

export default App;
