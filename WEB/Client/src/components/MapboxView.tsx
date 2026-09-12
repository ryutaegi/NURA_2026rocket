import 'mapbox-gl/dist/mapbox-gl.css';
import { useEffect, useRef } from 'react';
import mapboxgl, { CustomLayerInterface } from 'mapbox-gl';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import { RocketTelemetry } from './MainPage';

interface MapboxViewProps {
  telemetry: RocketTelemetry;
  resetLine?: boolean;
}

mapboxgl.accessToken = import.meta.env.VITE_MAPBOX_ACCESS_TOKEN;

const modelOrigin = [126.9780, 37.5665] as [number, number];
const modelAltitude = 0;
const MAX_POINTS = 10000;
// ~3m in Mercator units: 3 × 3.03e-8 ≈ 9e-8
// 동일 GPS 좌표(패킷 0-90)가 연속으로 들어올 때 Line 방향벡터가 0이 되는 버그를 막음
const MIN_MERCATOR_MOVE = 9e-8;

const modelAsMercatorCoordinate = mapboxgl.MercatorCoordinate.fromLngLat(
  { lng: modelOrigin[0], lat: modelOrigin[1] },
  modelAltitude
);

export default function MapboxView({ telemetry, resetLine }: MapboxViewProps) {
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const mapRef          = useRef<mapboxgl.Map | null>(null);
  const rocketGroupRef  = useRef<THREE.Group | null>(null);
  const trajGeoRef      = useRef<THREE.BufferGeometry | null>(null);
  const pointIndexRef   = useRef(0);
  const lastPosRef      = useRef<{ x: number; y: number; z: number } | null>(null);

  useEffect(() => {
    if (mapRef.current || !mapContainerRef.current) return;

    const map = new mapboxgl.Map({
      container: mapContainerRef.current,
      style: 'mapbox://styles/mapbox/light-v11',
      center: [telemetry.longitude, telemetry.latitude],
      zoom: 18,
      pitch: 75,
      bearing: -60,
      antialias: true,
    });

    mapRef.current = map;

    const resizeObserver = new ResizeObserver(() => map.resize());
    resizeObserver.observe(mapContainerRef.current);

    const customLayer: CustomLayerInterface = {
      id: '3d-scene',
      type: 'custom',
      renderingMode: '3d',
      onAdd(_map, gl) {
        (this as any).camera   = new THREE.Camera();
        (this as any).scene    = new THREE.Scene();
        (this as any).sceneMap = _map;

        // 조명
        const amb = new THREE.AmbientLight(0xffffff, 1.5);
        (this as any).scene.add(amb);
        const dir = new THREE.DirectionalLight(0xffffff, 2.5);
        dir.position.set(0, -70, 100).normalize();
        (this as any).scene.add(dir);

        // 로켓 그룹
        const rocketGroup = new THREE.Group();
        rocketGroupRef.current = rocketGroup;
        (this as any).scene.add(rocketGroup);

        const modelFixGroup = new THREE.Group();
        modelFixGroup.rotation.x = Math.PI / 2;
        rocketGroup.add(modelFixGroup);

        const loader = new GLTFLoader();
        loader.load(
          '/sci-fi_rocket/scene.gltf',
          (gltf) => {
            const model = gltf.scene;
            const scale = modelAsMercatorCoordinate.meterInMercatorCoordinateUnits() * 10;
            model.scale.set(scale, scale, scale);
            modelFixGroup.add(model);
          },
          undefined,
          (err) => console.error('GLTF 로드 오류:', err)
        );

        // ── 궤적 시각화 ──────────────────────────────────────────────────
        // Line2는 방향벡터가 0인 중복 점에서 무너짐 → Points + Line으로 교체
        // Points: 6px 빨간 점(sizeAttenuation:false = 화면 픽셀 고정 크기)
        // Line:   1px 연결선
        // 두 오브젝트가 동일 BufferGeometry 공유 → 한 번 업데이트로 둘 다 갱신
        const trajGeo = new THREE.BufferGeometry();
        const positions = new Float32Array(MAX_POINTS * 3);
        trajGeo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        trajGeo.setDrawRange(0, 0);
        trajGeoRef.current = trajGeo;

        const pointsMat = new THREE.PointsMaterial({
          color: 0xff2222,
          size: 6,
          depthTest: false,
          depthWrite: false,
          transparent: true,
          sizeAttenuation: false,  // 화면 픽셀 단위 고정 크기
        });
        const trajPoints = new THREE.Points(trajGeo, pointsMat);
        trajPoints.renderOrder = 999;
        trajPoints.frustumCulled = false;  // pre-alloc 버퍼가 (0,0,0)으로 채워져 있어 바운딩 스피어가 Seoul → frustum culled됨 방지
        (this as any).scene.add(trajPoints);

        const lineMat = new THREE.LineBasicMaterial({
          color: 0xff2222,
          depthTest: false,
          depthWrite: false,
          transparent: true,
        });
        const trajLine = new THREE.Line(trajGeo, lineMat);
        trajLine.renderOrder = 998;
        trajLine.frustumCulled = false;
        (this as any).scene.add(trajLine);

        (this as any).renderer = new THREE.WebGLRenderer({
          canvas: _map.getCanvas(),
          context: gl,
          antialias: true,
        });
        (this as any).renderer.autoClear = false;
      },
      render(_gl, matrix) {
        const m = new THREE.Matrix4().fromArray(matrix);
        const l = new THREE.Matrix4().makeTranslation(
          modelAsMercatorCoordinate.x,
          modelAsMercatorCoordinate.y,
          modelAsMercatorCoordinate.z
        );
        (this as any).camera.projectionMatrix = m.multiply(l);
        (this as any).renderer.resetState();
        (this as any).renderer.render((this as any).scene, (this as any).camera);
        (this as any).sceneMap.triggerRepaint();
      },
    };

    map.on('load', () => {
      map.addSource('mapbox-dem', {
        type: 'raster-dem',
        url: 'mapbox://mapbox.mapbox-terrain-dem-v1',
      });
      map.setTerrain({ source: 'mapbox-dem', exaggeration: 1.5 });
      map.addLayer({
        id: 'sky',
        type: 'sky',
        paint: { 'sky-type': 'atmosphere', 'sky-atmosphere-sun-intensity': 5 },
      });
      map.addLayer(customLayer);

      setTimeout(() => map.resize(), 100);
      setTimeout(() => map.resize(), 1000);
    });

    const handleResize = () => map.resize();
    window.addEventListener('resize', handleResize);

    return () => {
      resizeObserver.disconnect();
      window.removeEventListener('resize', handleResize);
      map.remove();
      mapRef.current         = null;
      rocketGroupRef.current = null;
      trajGeoRef.current     = null;
      pointIndexRef.current  = 0;
      lastPosRef.current     = null;
    };
  }, []);

  // 리플레이 시작 시 궤적 초기화
  useEffect(() => {
    const geo = trajGeoRef.current;
    if (!geo) return;
    const positions = geo.attributes.position.array as Float32Array;
    positions.fill(0);
    geo.attributes.position.needsUpdate = true;
    geo.setDrawRange(0, 0);
    pointIndexRef.current = 0;
    lastPosRef.current    = null;
  }, [resetLine]);

  // 텔레메트리 업데이트
  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const currentMercator = mapboxgl.MercatorCoordinate.fromLngLat(
      { lng: telemetry.longitude, lat: telemetry.latitude },
      telemetry.altitude
    );

    const rel = new THREE.Vector3(
      currentMercator.x - modelAsMercatorCoordinate.x,
      currentMercator.y - modelAsMercatorCoordinate.y,
      currentMercator.z - modelAsMercatorCoordinate.z
    );

    // 로켓 위치·자세
    if (rocketGroupRef.current) {
      rocketGroupRef.current.position.copy(rel);
      rocketGroupRef.current.quaternion.set(
        telemetry.q1, telemetry.q2, telemetry.q3, telemetry.q0
      );
    }

    // 궤적 포인트 추가 (중복 좌표 필터링)
    const geo = trajGeoRef.current;
    if (geo) {
      const last = lastPosRef.current;
      const moved = !last ||
        Math.abs(rel.x - last.x) > MIN_MERCATOR_MOVE ||
        Math.abs(rel.y - last.y) > MIN_MERCATOR_MOVE ||
        Math.abs(rel.z - last.z) > MIN_MERCATOR_MOVE;

      if (moved) {
        const idx = pointIndexRef.current;
        if (idx < MAX_POINTS) {
          const buf = geo.attributes.position.array as Float32Array;
          buf[idx * 3]     = rel.x;
          buf[idx * 3 + 1] = rel.y;
          buf[idx * 3 + 2] = rel.z;
          geo.attributes.position.needsUpdate = true;
          geo.setDrawRange(0, idx + 1);
          pointIndexRef.current++;
          lastPosRef.current = { x: rel.x, y: rel.y, z: rel.z };
        }
      }
    }

    map.triggerRepaint();
    map.flyTo({
      center: [telemetry.longitude, telemetry.latitude],
      speed: 0.8, curve: 1, essential: true,
    });
  }, [telemetry]);

  return (
    <div className="absolute inset-0 w-full h-full">
      <div ref={mapContainerRef} className="w-full h-full" />
      <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-2 rounded-lg pointer-events-none z-30">
        <div className="text-xs text-gray-400">Mapbox 3D View</div>
        <div className="text-sm">위도: {telemetry.latitude.toFixed(6)}°</div>
        <div className="text-sm">경도: {telemetry.longitude.toFixed(6)}°</div>
        <div className="text-sm font-bold text-yellow-400">고도: {telemetry.altitude.toFixed(1)} m</div>
      </div>
    </div>
  );
}
