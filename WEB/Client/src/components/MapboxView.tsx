import { useEffect, useRef, useState } from 'react';
import mapboxgl, { CustomLayerInterface } from 'mapbox-gl';
import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js';
import 'mapbox-gl/dist/mapbox-gl.css';
import { RocketTelemetry } from './MainPage';

interface MapboxViewProps {
  telemetry: RocketTelemetry;
}

// Vite 환경 변수를 사용하여 Mapbox Access Token 설정
mapboxgl.accessToken = import.meta.env.VITE_MAPBOX_ACCESS_TOKEN; 

const modelOrigin = [126.9780, 37.5665] as [number, number];
const modelAltitude = 0;
const modelRotate = [Math.PI / 2, 0, 0];

const modelAsMercatorCoordinate = mapboxgl.MercatorCoordinate.fromLngLat(
  modelOrigin,
  modelAltitude
);

const modelTransform = {
  translateX: modelAsMercatorCoordinate.x,
  translateY: modelAsMercatorCoordinate.y,
  translateZ: modelAsMercatorCoordinate.z,
  rotateX: modelRotate[0],
  rotateY: modelRotate[1],
  rotateZ: modelRotate[2],
  scale: modelAsMercatorCoordinate.meterInMercatorCoordinateUnits() * 20, // 모델 크기 조절
};

export default function MapboxView({ telemetry }: MapboxViewProps) {
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<mapboxgl.Map | null>(null);
  const modelRef = useRef<THREE.Group>();
  const customLayerRef = useRef<CustomLayerInterface>();
  const [trajectory, setTrajectory] = useState<[number, number][]>([]); // 로켓 궤적 저장을 위한 상태

  useEffect(() => {
    if (mapRef.current || !mapContainerRef.current) return;

    const map = new mapboxgl.Map({
      container: mapContainerRef.current,
      style: 'mapbox://styles/mapbox/dark-v11', //satellite-streets-v12,
      center: [telemetry.longitude, telemetry.latitude],
      zoom: 18,
      pitch: 75,
      bearing: -60,
      antialias: true,
    });

    mapRef.current = map;

    // Three.js 3D 모델을 위한 커스텀 레이어
    const customLayer: CustomLayerInterface = {
      id: '3d-model',
      type: 'custom',
      renderingMode: '3d',
      onAdd: function (map, gl) {
        this.camera = new THREE.Camera();
        this.scene = new THREE.Scene();

        // 조명 추가
        const ambientLight = new THREE.AmbientLight(0xffffff, 1.5);
        this.scene.add(ambientLight);
        const directionalLight = new THREE.DirectionalLight(0xffffff, 2.5);
        directionalLight.position.set(0, -70, 100).normalize();
        this.scene.add(directionalLight);

        // GLTF 모델 로드
        const loader = new GLTFLoader();
        loader.load(
          '/sci-fi_rocket/scene.gltf', // Vite는 public 폴더를 자동으로 서빙
          (gltf) => {
            const model = gltf.scene;
            
            // 모델의 바닥을 고도 0에 맞추기 위한 오프셋 계산
            const box = new THREE.Box3().setFromObject(model);
            const size = new THREE.Vector3();
            box.getSize(size);
            // 모델의 중심이 아닌, 바닥을 기준으로 위치를 잡기 위해 y축으로 높이의 절반만큼 이동
            model.position.y = size.y / 2;

            modelRef.current = model;
            this.scene.add(model);
          },
          undefined,
          (error) => {
            console.error('3D 모델을 로드하는 중 오류 발생:', error);
            alert('3D 모델 로딩에 실패했습니다. /public/sci-fi_rocket/ 폴더에 모델 파일이 있는지 확인하세요.');
          }
        );

        this.map = map;
        this.renderer = new THREE.WebGLRenderer({
          canvas: map.getCanvas(),
          context: gl,
          antialias: true,
        });
        this.renderer.autoClear = false;
      },
      render: function (gl, matrix) {
        const rotationX = new THREE.Matrix4().makeRotationX(modelTransform.rotateX);
        const rotationY = new THREE.Matrix4().makeRotationY(modelTransform.rotateY);
        const rotationZ = new THREE.Matrix4().makeRotationZ(modelTransform.rotateZ);
        
        const m = new THREE.Matrix4().fromArray(matrix);
        const l = new THREE.Matrix4()
          .makeTranslation(
            modelTransform.translateX,
            modelTransform.translateY,
            modelTransform.translateZ
          )
          .scale(
            new THREE.Vector3(
              modelTransform.scale,
              -modelTransform.scale,
              modelTransform.scale
            )
          )
          .multiply(rotationX)
          .multiply(rotationY)
          .multiply(rotationZ);

        this.camera.projectionMatrix = m.multiply(l);
        this.renderer.resetState();
        this.renderer.render(this.scene, this.camera);
        this.map.triggerRepaint();
      },
    };

    customLayerRef.current = customLayer;

    map.on('load', () => {
      map.addLayer(customLayer);
      map.addSource('trajectory', {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: {},
          geometry: {
            type: 'LineString',
            coordinates: [],
          },
        },
      });

      map.addLayer({
        id: 'trajectory-line',
        type: 'line',
        source: 'trajectory',
        layout: {
          'line-join': 'round',
          'line-cap': 'round',
        },
        paint: {
          'line-color': '#FF0000', // 빨간색 선
          'line-width': 3,
        },
      });
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
    });

    return () => {
      map.remove();
      mapRef.current = null;
    };
  }, []);

  // 텔레메트리 데이터 변경 시 모델 위치 및 방향 업데이트
  useEffect(() => {
    if (mapRef.current && modelRef.current) {
        const map = mapRef.current;
        const model = modelRef.current;

        // 궤적 업데이트
        setTrajectory((prev) => [...prev, [telemetry.longitude, telemetry.latitude]]);
        
        // 1. 위치 업데이트
        const mercator = mapboxgl.MercatorCoordinate.fromLngLat(
            { lng: telemetry.longitude, lat: telemetry.latitude },
            telemetry.altitude
        );
        
        modelTransform.translateX = mercator.x;
        modelTransform.translateY = mercator.y;
        modelTransform.translateZ = mercator.z;

        // 2. 방향 업데이트 (pitch, roll, yaw -> x, y, z)
        // three.js는 Radians를 사용, ZYX 순서로 적용
        model.rotation.set(
            telemetry.pitch * (Math.PI / 180),
            telemetry.yaw * (Math.PI / 180), // Yaw를 Y축 회전으로 매핑
            telemetry.roll * (Math.PI / 180)  // Roll을 Z축 회전으로 매핑
        );
        
        map.triggerRepaint();

        // 3. 카메라 따라가기
        map.flyTo({
            center: [telemetry.longitude, telemetry.latitude],
            speed: 0.8,
            curve: 1,
            essential: true, // 애니메이션 중 사용자 입력이 있어도 중단되지 않음
        });
    }
  }, [telemetry]);

  // 궤적 데이터 변경 시 Mapbox Source 업데이트
  useEffect(() => {
    if (mapRef.current) {
      const map = mapRef.current;
      if (map.getSource('trajectory')) {
        (map.getSource('trajectory') as mapboxgl.GeoJSONSource).setData({
          type: 'Feature',
          properties: {},
          geometry: {
            type: 'LineString',
            coordinates: trajectory,
          },
        });
      }
    }
  }, [trajectory]);

  return (
    <div className="relative w-full h-full">
      <div ref={mapContainerRef} className="w-full h-full" />
      <div className="absolute top-4 left-4 bg-black/70 backdrop-blur-sm text-white px-4 py-2 rounded-lg pointer-events-none">
        <div className="text-xs text-gray-400">Mapbox 3D View</div>
        <div className="text-sm">위도: {telemetry.latitude.toFixed(6)}°</div>
        <div className="text-sm">경도: {telemetry.longitude.toFixed(6)}°</div>
        <div className="text-xs text-gray-400 mt-2">3D 모델: /sci-fi_rocket/scene.gltf</div>
      </div>
    </div>
  );
}
