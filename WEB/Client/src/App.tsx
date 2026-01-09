import { BrowserRouter as Router, Routes, Route, Link, useLocation } from 'react-router-dom';
import { Rocket, MapPin, History } from 'lucide-react';
import MainPage from './components/MainPage';
import RecoveryPage from './components/RecoveryPage';
import LaunchHistoryPage from './components/LaunchHistoryPage';

function Navigation() {
  const location = useLocation();
  
  const isActive = (path: string) => location.pathname === path;
  
  return (
    <nav className="bg-gray-900 border-b border-gray-800">
      <div className="max-w-7xl mx-auto px-4 sm:px-6 lg:px-8">
        <div className="flex justify-between h-16">
          <div className="flex">
            <div className="flex-shrink-0 flex items-center">
              <Rocket className="h-8 w-8 text-blue-500" />
              <span className="ml-2 text-xl text-white">MACH1n(e)</span>
            </div>
            <div className="ml-6 flex space-x-4">
              <Link
                to="/"
                className={`inline-flex items-center px-4 py-2 border-b-2 ${
                  isActive('/')
                    ? 'border-blue-500 text-white'
                    : 'border-transparent text-gray-400 hover:text-white hover:border-gray-300'
                }`}
              >
                <Rocket className="h-5 w-5 mr-2" />
                메인
              </Link>
              <Link
                to="/recovery"
                className={`inline-flex items-center px-4 py-2 border-b-2 ${
                  isActive('/recovery')
                    ? 'border-blue-500 text-white'
                    : 'border-transparent text-gray-400 hover:text-white hover:border-gray-300'
                }`}
              >
                <MapPin className="h-5 w-5 mr-2" />
                로켓 회수
              </Link>
              <Link
                to="/history"
                className={`inline-flex items-center px-4 py-2 border-b-2 ${
                  isActive('/history')
                    ? 'border-blue-500 text-white'
                    : 'border-transparent text-gray-400 hover:text-white hover:border-gray-300'
                }`}
              >
                <History className="h-5 w-5 mr-2" />
                발사 기록
              </Link>
            </div>
          </div>
        </div>
      </div>
    </nav>
  );
}

export default function App() {
  return (
    <Router>
      <div className="min-h-screen bg-gray-950">
        <Navigation />
        <Routes>
          <Route path="/" element={<MainPage />} />
          <Route path="/recovery" element={<RecoveryPage />} />
          <Route path="/history" element={<LaunchHistoryPage />} />
        </Routes>
      </div>
    </Router>
  );
}
