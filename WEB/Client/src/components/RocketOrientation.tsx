import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls.js';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { RocketTelemetry } from './MainPage';

interface RocketOrientationProps {
  telemetry: RocketTelemetry;
}

export default function RocketOrientation({ telemetry }: RocketOrientationProps) {
  const containerRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<THREE.Scene | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const rocketRef = useRef<THREE.Group | null>(null);
  const animationFrameRef = useRef<number>();

  useEffect(() => {
    if (!containerRef.current) return;

    // Scene 설정
    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x0a0a0a);
    sceneRef.current = scene;

    // Camera 설정
    const camera = new THREE.PerspectiveCamera(
      75,
      containerRef.current.clientWidth / containerRef.current.clientHeight,
      0.1,
      1000
    );
    camera.position.z = 5;
    camera.position.y = 2;
    camera.lookAt(0, 0, 0);
    cameraRef.current = camera;

    // Renderer 설정
    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setSize(containerRef.current.clientWidth, containerRef.current.clientHeight);
    containerRef.current.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    // OrbitControls 설정
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.enableZoom = true;

    // 로켓 그룹 생성
    const rocket = new THREE.Group();
    rocket.position.y = 0; // 그리드 높이에 맞춤
    scene.add(rocket);
    rocketRef.current = rocket;
    
    // GLTF 모델 로더
    const loader = new GLTFLoader();
    loader.load(
      '/sci-fi_rocket/scene.gltf',
      (gltf) => {
        const model = gltf.scene;

        // 경계 상자 계산
        const box = new THREE.Box3().setFromObject(model);
        const size = new THREE.Vector3();
        box.getSize(size);
        const center = box.getCenter(new THREE.Vector3());

        // X, Y, Z 축 모두 모델의 중심으로 이동
        model.position.x -= center.x;
        model.position.y -= center.y; // 회전축을 로켓의 중심으로 변경
        model.position.z -= center.z;

        // 모델 크기를 약 4 유닛에 맞게 스케일 조절
        const maxDim = Math.max(size.x, size.y, size.z);
        const scale = 4 / maxDim;
        model.scale.set(scale, scale, scale);

        // 메인 로켓 그룹에 모델 추가
        rocket.add(model);
      },
      undefined,
      (error) => {
        console.error('An error happened while loading the model:', error);
      }
    );


    // 조명
    const ambientLight = new THREE.AmbientLight(0xffffff, 0.8);
    scene.add(ambientLight);

    const directionalLight = new THREE.DirectionalLight(0xffffff, 1);
    directionalLight.position.set(5, 5, 5);
    scene.add(directionalLight);

    // 그리드 헬퍼
    const gridHelper = new THREE.GridHelper(10, 10, 0x1e293b, 0x1e293b); //0x3b82f6
    gridHelper.position.y = 0;
    scene.add(gridHelper);

    // 축 헬퍼
    const axesHelper = new THREE.AxesHelper(3);
    scene.add(axesHelper);

    // 애니메이션 루프
    const animate = () => {
      animationFrameRef.current = requestAnimationFrame(animate);
      controls.update(); // Damping을 위해 매 프레임 업데이트
      renderer.render(scene, camera);
    };
    animate();

    // 리사이즈 핸들러
    const handleResize = () => {
      if (!containerRef.current || !camera || !renderer) return;
      camera.aspect = containerRef.current.clientWidth / containerRef.current.clientHeight;
      camera.updateProjectionMatrix();
      renderer.setSize(containerRef.current.clientWidth, containerRef.current.clientHeight);
    };
    window.addEventListener('resize', handleResize);

    return () => {
      window.removeEventListener('resize', handleResize);
      if (animationFrameRef.current) {
        cancelAnimationFrame(animationFrameRef.current);
      }
      controls.dispose(); // 컨트롤러 리소스 해제
      if (containerRef.current && renderer.domElement) {
        containerRef.current.removeChild(renderer.domElement);
      }
      renderer.dispose();
    };
  }, []);

  // 로켓 회전 업데이트
  useEffect(() => {
    if (rocketRef.current) {
      rocketRef.current.rotation.order = 'ZYX';
      // Pitch (X축), Roll (Z축), Yaw (Y축)
      rocketRef.current.rotation.x = -(telemetry.pitch * Math.PI) / 180;
      rocketRef.current.rotation.z = (telemetry.roll * Math.PI) / 180;
      rocketRef.current.rotation.y = -(telemetry.yaw * Math.PI) / 180;
    }
  }, [telemetry.pitch, telemetry.roll, telemetry.yaw]);

  return (
    <div className="relative w-full h-full">
      <div ref={containerRef} className="w-full h-full" />
      
      {/* 정보 오버레이 */}
      <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-3 rounded-lg space-y-1">
        <div className="text-xs text-gray-400 mb-2">로켓 자세 (Three.js)</div>
        <div className="grid grid-cols-3 gap-4 text-sm">
        <div>
            <div className="text-xs text-gray-400">Roll</div>
            <div className="text-blue-400">{telemetry.roll.toFixed(1)}°</div>
          </div>
          <div>
            <div className="text-xs text-gray-400">Pitch</div>
            <div className="text-red-400">{telemetry.pitch.toFixed(1)}°</div>
          </div>
          <div>
            <div className="text-xs text-gray-400">Yaw</div>
            <div className="text-green-400">{telemetry.yaw.toFixed(1)}°</div>
          </div>
        </div>
      </div>

      {/* 좌표축 라벨 */}
      <div className="absolute bottom-4 right-4 bg-black/70 backdrop-blur-sm text-white px-4 py-3 rounded-lg text-xs">
      <div className="flex items-center gap-2 mb-1">
          <div className="w-4 h-0.5 bg-blue-500" />
          <span>X (roll)</span>
        </div>

        <div className="flex items-center gap-2 mb-1">
          <div className="w-4 h-0.5 bg-red-500" />
          <span>Y (Pitch)</span>
        </div>
        <div className="flex items-center gap-2">
          <div className="w-4 h-0.5 bg-green-500" />
          <span>Z (Yaw)</span>
        </div>
        
      </div>
    </div>
  );
}
