import 'mapbox-gl/dist/mapbox-gl.css';
import { useEffect, useRef } from 'react';
import mapboxgl, { CustomLayerInterface } from 'mapbox-gl';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { RocketTelemetry } from './MainPage';

interface MapboxViewProps {
  telemetry: RocketTelemetry;
}

// Vite 환경 변수를 사용하여 Mapbox Access Token 설정
mapboxgl.accessToken = import.meta.env.VITE_MAPBOX_ACCESS_TOKEN;

const modelOrigin = [126.9780, 37.5665] as [number, number];
const modelAltitude = 0;

const modelAsMercatorCoordinate = mapboxgl.MercatorCoordinate.fromLngLat(
  { lng: modelOrigin[0], lat: modelOrigin[1] },
  modelAltitude
);

export default function MapboxView({ telemetry }: MapboxViewProps) {
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<mapboxgl.Map | null>(null);
  const modelRef = useRef<THREE.Group>(null!);
  const lineRef = useRef<THREE.Line>(null!);
  const customLayerRef = useRef<CustomLayerInterface>(null!);
  const pointIndexRef = useRef(0);

  useEffect(() => {
    if (mapRef.current || !mapContainerRef.current) return;

    const map = new mapboxgl.Map({
      container: mapContainerRef.current,
      style: 'mapbox://styles/mapbox/dark-v11',
      center: [telemetry.longitude, telemetry.latitude],
      zoom: 18,
      pitch: 75,
      bearing: -60,
      antialias: true,
    });

    mapRef.current = map;

    const resizeObserver = new ResizeObserver(() => {
      map.resize();
    });

    resizeObserver.observe(mapContainerRef.current);

    // Three.js 3D 모델을 위한 커스텀 레이어
    const customLayer: CustomLayerInterface = {
      id: '3d-model',
      type: 'custom',
      renderingMode: '3d',
      onAdd: function (map, gl) {
        (this as any).camera = new THREE.Camera();
        (this as any).scene = new THREE.Scene();

        // 조명 추가
        const ambientLight = new THREE.AmbientLight(0xffffff, 1.5);
        (this as any).scene.add(ambientLight);
        const directionalLight = new THREE.DirectionalLight(0xffffff, 2.5);
        directionalLight.position.set(0, -70, 100).normalize();
        (this as any).scene.add(directionalLight);

        // GLTF 모델 로드
        const loader = new GLTFLoader();
        loader.load(
          '/sci-fi_rocket/scene.gltf',
          (gltf) => {
            const model = gltf.scene;
            const scale = modelAsMercatorCoordinate.meterInMercatorCoordinateUnits() * 30;
            model.scale.set(scale, scale, scale);
            modelRef.current = model;
            (this as any).scene.add(model);
          },
          undefined,
          (error) => {
            console.error('3D 모델을 로드하는 중 오류 발생:', error);
          }
        );

        // Trajectory Line
        const lineMaterial = new THREE.LineBasicMaterial({ color: 0xff0000 });
        const MAX_POINTS = 10000;
        const lineGeometry = new THREE.BufferGeometry();
        const positions = new Float32Array(MAX_POINTS * 3);
        lineGeometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        const line = new THREE.Line(lineGeometry, lineMaterial);
        lineRef.current = line;
        (this as any).scene.add(line);

        (this as any).map = map;
        (this as any).renderer = new THREE.WebGLRenderer({
          canvas: map.getCanvas(),
          context: gl,
          antialias: true,
        });
        (this as any).renderer.autoClear = false;
      },
      render: function (gl, matrix) {
        const m = new THREE.Matrix4().fromArray(matrix);
        const l = new THREE.Matrix4().makeTranslation(
          modelAsMercatorCoordinate.x,
          modelAsMercatorCoordinate.y,
          modelAsMercatorCoordinate.z
        );

        (this as any).camera.projectionMatrix = m.multiply(l);
        (this as any).renderer.resetState();
        (this as any).renderer.render((this as any).scene, (this as any).camera);
        (this as any).map.triggerRepaint();
      },
    };

    customLayerRef.current = customLayer;

    map.on('load', () => {
      map.addLayer(customLayer);
      map.addSource('mapbox-dem', {
        type: 'raster-dem',
        url: 'mapbox://mapbox.mapbox-terrain-dem-v1',
      });
      map.setTerrain({ source: 'mapbox-dem', exaggeration: 1.5 });
      map.addLayer({
        id: 'sky',
        type: 'sky',
        paint: {
          'sky-type': 'atmosphere',
          'sky-atmosphere-sun-intensity': 5,
        },
      });

      // 초기 렌더링 시 리사이즈 강제 호출
      setTimeout(() => {
        map.resize();
      }, 100);
      setTimeout(() => {
        map.resize();
      }, 1000);
    });

    // 윈도우 리사이즈 이벤트 대응
    const handleWindowResize = () => map.resize();
    window.addEventListener('resize', handleWindowResize);

    return () => {
      resizeObserver.disconnect();
      window.removeEventListener('resize', handleWindowResize);
      map.remove();
      mapRef.current = null;
    };
  }, []);

  useEffect(() => {
    if (mapRef.current && modelRef.current && lineRef.current && customLayerRef.current) {
      const map = mapRef.current;
      const model = modelRef.current;
      const line = lineRef.current;

      const currentMercator = mapboxgl.MercatorCoordinate.fromLngLat(
        { lng: telemetry.longitude, lat: telemetry.latitude },
        telemetry.altitude
      );

      const relativePosition = new THREE.Vector3(
        currentMercator.x - modelAsMercatorCoordinate.x,
        currentMercator.y - modelAsMercatorCoordinate.y,
        currentMercator.z - modelAsMercatorCoordinate.z
      );

      model.position.copy(relativePosition);
      model.rotation.set(
        telemetry.roll * (Math.PI / 180),
        telemetry.pitch * (Math.PI / 180) + Math.PI / 2,
        telemetry.yaw * (Math.PI / 180)
      );

      const index = pointIndexRef.current;
      const linePositions = line.geometry.attributes.position.array as Float32Array;

      if (index < (linePositions.length / 3)) {
        linePositions[index * 3] = relativePosition.x;
        linePositions[index * 3 + 1] = relativePosition.y;
        linePositions[index * 3 + 2] = relativePosition.z;

        line.geometry.attributes.position.needsUpdate = true;
        line.geometry.setDrawRange(0, index + 1);
        pointIndexRef.current++;
      }

      map.triggerRepaint();
      map.flyTo({
        center: [telemetry.longitude, telemetry.latitude],
        speed: 0.8,
        curve: 1,
        essential: true,
      });
    }
  }, [telemetry]);

  return (
    <div className="absolute inset-0 w-full h-full">
      <div ref={mapContainerRef} className="w-full h-full" />
      <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-2 rounded-lg pointer-events-none z-30">
        <div className="text-xs text-gray-400">Mapbox 3D View</div>
        <div className="text-sm">위도: {telemetry.latitude.toFixed(6)}°</div>
        <div className="text-sm">경도: {telemetry.longitude.toFixed(6)}°</div>
      </div>
    </div>
  );
}
