import React, { useState, useEffect } from 'react';
import { BrowserRouter as Router, Routes, Route, Link, useLocation } from 'react-router-dom';
import { Rocket, MapPin, History } from 'lucide-react';
import MainPage from './components/MainPage';
import RecoveryPage from './components/RecoveryPage';
import LaunchHistoryPage from './components/LaunchHistoryPage';

function Navigation() {
  const location = useLocation();

  const isActive = (path: string) => location.pathname === path;

  const NavLink = ({ to, icon: Icon, children }: { to: string, icon: any, children: React.ReactNode }) => (
    <Link
      to={to}
      className={`inline-flex items-center px-4 h-full border-b-2 transition-colors text-sm font-semibold ${isActive(to)
        ? 'border-[#ff385c] text-[#222222]'
        : 'border-transparent text-[#6a6a6a] hover:text-[#222222] hover:border-[#dddddd]'
        }`}
    >
      <Icon className="h-4 w-4 mr-2" />
      {children}
    </Link>
  );

  return (
    <nav className="bg-white border-b border-[#dddddd] sticky top-0 z-50 w-full">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex justify-between h-16 items-center">
          <span className="text-xl font-black text-[#ff385c] tracking-tighter flex-shrink-0">MACH1n(e)</span>
          <div className="flex items-center h-full">
            <NavLink to="/" icon={Rocket}>메인</NavLink>
            <NavLink to="/recovery" icon={MapPin}>로켓 회수</NavLink>
            <NavLink to="/history" icon={History}>발사 기록</NavLink>
          </div>
        </div>
      </div>
    </nav>
  );
}

export default function App() {
  const [centerAlign, setCenterAlign] = useState(false);
  const [emergencyEjection, setEmergencyEjection] = useState(false);

  useEffect(() => {
    const handleKeyPress = (event: KeyboardEvent) => {
      if (event.key === '!') {
        setCenterAlign(prev => {
          console.log('Center align toggled:', !prev);
          return !prev;
        });
      } else if (event.key === '@') {
        setEmergencyEjection(true);
        console.log('Emergency ejection triggered!');
        setTimeout(() => setEmergencyEjection(false), 2000);
      }
    };

    window.addEventListener('keydown', handleKeyPress);
    return () => window.removeEventListener('keydown', handleKeyPress);
  }, []);

  return (
    <Router>
      <div className="min-h-screen bg-[#f7f7f7]">
        <Navigation />
        <Routes>
          <Route path="/" element={<MainPage centerAlign={centerAlign} emergencyEjection={emergencyEjection} />} />
          <Route path="/recovery" element={<RecoveryPage />} />
          <Route path="/history" element={<LaunchHistoryPage />} />
        </Routes>
      </div>
    </Router>
  );
}
