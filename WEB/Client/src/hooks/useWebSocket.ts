import { useEffect, useRef, useState } from 'react';

const DEFAULT_WS_URL = 'ws://localhost:3001';

export interface WebSocketMessage {
  type: string;
  data?: any;
  [key: string]: any; // Allow additional fields
}

export function useWebSocket() {
  const [isConnected, setIsConnected] = useState(false);
  const [lastMessage, setLastMessage] = useState<WebSocketMessage | null>(null);
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    // 환경 변수 또는 로컬 스토리지에서 URL 가져오기 (배포 시 유연성 확보)
    const wsUrl = import.meta.env.VITE_WS_URL || localStorage.getItem('ws_url') || DEFAULT_WS_URL;

    console.log(`WebSocket 연결 시도: ${wsUrl}`);
    const ws = new WebSocket(wsUrl);
    wsRef.current = ws;

    ws.onopen = () => {
      console.log('WebSocket 연결됨');
      setIsConnected(true);
    };

    ws.onmessage = (event) => {
      try {
        const message = JSON.parse(event.data);
        setLastMessage(message);
      } catch (error) {
        console.error('메시지 파싱 에러:', error);
      }
    };

    ws.onerror = (error) => {
      console.error('WebSocket 에러:', error);
    };

    ws.onclose = () => {
      console.log('WebSocket 연결 종료');
      setIsConnected(false);
      // 자동 재연결 로직 (선택 사항)
      // setTimeout(() => { ... }, 3000);
    };

    return () => {
      ws.close();
    };
  }, []);

  const sendMessage = (message: WebSocketMessage) => {
    if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(message));
    } else {
      console.error('WebSocket이 연결되지 않았습니다');
    }
  };

  return {
    isConnected,
    lastMessage,
    sendMessage,
  };
}
