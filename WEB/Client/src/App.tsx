import React, { useState, useEffect } from 'react';
import { BrowserRouter as Router, Routes, Route, Link, useLocation } from 'react-router-dom';
import { Rocket, MapPin, History, Menu, X } from 'lucide-react';
import MainPage from './components/MainPage';
import RecoveryPage from './components/RecoveryPage';
import LaunchHistoryPage from './components/LaunchHistoryPage';

function Navigation() {
  const location = useLocation();
  const [isMenuOpen, setIsMenuOpen] = useState(false);

  const isActive = (path: string) => location.pathname === path;

  const NavLink = ({ to, icon: Icon, children }: { to: string, icon: any, children: React.ReactNode }) => (
    <Link
      to={to}
      onClick={() => setIsMenuOpen(false)}
      className={`inline-flex items-center px-4 py-2 border-b-2 transition-colors ${isActive(to)
        ? 'border-blue-500 text-white'
        : 'border-transparent text-gray-400 hover:text-white hover:border-gray-300'
        }`}
    >
      <Icon className="h-5 w-5 mr-2" />
      {children}
    </Link>
  );

  const MobileNavLink = ({ to, icon: Icon, children }: { to: string, icon: any, children: React.ReactNode }) => (
    <Link
      to={to}
      onClick={() => setIsMenuOpen(false)}
      className={`flex items-center px-4 py-3 rounded-lg text-base font-medium transition-colors ${isActive(to)
        ? 'bg-blue-600 text-white'
        : 'text-gray-300 hover:bg-gray-800'
        }`}
    >
      <Icon className="h-5 w-5 mr-3" />
      {children}
    </Link>
  );

  return (
    <nav className="bg-gray-900 border-b border-gray-800 sticky top-0 z-50 w-full overflow-hidden">
      <style>{`
        @media (min-width: 768px) {
          .desktop-only { display: flex !important; }
          .mobile-only { display: none !important; }
        }
        @media (max-width: 767px) {
          .desktop-only { display: none !important; }
          .mobile-only { display: flex !important; }
        }
      `}</style>
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex justify-between h-16">
          <div className="flex w-full justify-between items-center">
            <div className="flex-shrink-0 flex items-center">
              <span className="text-2xl font-black text-white tracking-tighter">MACH1n(e)</span>
            </div>

            {/* Navigation Right Side */}
            <div className="flex items-center">
              {/* Desktop Menu */}
              <div className="desktop-only md:items-center md:space-x-4">
                <NavLink to="/" icon={Rocket}>메인</NavLink>
                <NavLink to="/recovery" icon={MapPin}>로켓 회수</NavLink>
                <NavLink to="/history" icon={History}>발사 기록</NavLink>
              </div>

              {/* Mobile Menu Button */}
              <div className="mobile-only items-center ml-2">
                <button
                  onClick={() => setIsMenuOpen(!isMenuOpen)}
                  className="text-gray-400 hover:text-white p-2 rounded-md focus:outline-none transition-colors"
                >
                  {isMenuOpen ? <X className="h-6 w-6" /> : <Menu className="h-6 w-6" />}
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>

      {/* Mobile Menu Overlay */}
      {isMenuOpen && (
        <div className="mobile-only flex-col bg-gray-900 border-b border-gray-800 px-2 pt-2 pb-3 space-y-1 shadow-2xl animate-in slide-in-from-top duration-200 w-full">
          <MobileNavLink to="/" icon={Rocket}>메인</MobileNavLink>
          <MobileNavLink to="/recovery" icon={MapPin}>로켓 회수</MobileNavLink>
          <MobileNavLink to="/history" icon={History}>발사 기록</MobileNavLink>
        </div>
      )}
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
        // For now, reset after a short delay or when action is acknowledged
        setTimeout(() => setEmergencyEjection(false), 2000);
      }
    };

    window.addEventListener('keydown', handleKeyPress);

    return () => {
      window.removeEventListener('keydown', handleKeyPress);
    };
  }, []);

  return (
    <Router>
      <div className="min-h-screen bg-gray-950">
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
