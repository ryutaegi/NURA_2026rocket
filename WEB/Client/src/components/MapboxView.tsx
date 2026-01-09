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

const modelAsMercatorCoordinate = mapboxgl.MercatorCoordinate.fromLngLat(
  modelOrigin,
  modelAltitude
);

export default function MapboxView({ telemetry }: MapboxViewProps) {
  const mapContainerRef = useRef<HTMLDivElement>(null);
  const mapRef = useRef<mapboxgl.Map | null>(null);
  const modelRef = useRef<THREE.Group>();
  const lineRef = useRef<THREE.Line>();
  const customLayerRef = useRef<CustomLayerInterface>();
  const pointIndexRef = useRef(0);

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
          '/sci-fi_rocket/scene.gltf',
          (gltf) => {
            const model = gltf.scene;
            const scale = modelAsMercatorCoordinate.meterInMercatorCoordinateUnits() * 30;
            model.scale.set(scale, scale, scale);
            modelRef.current = model;
            this.scene.add(model);
          },
          undefined,
          (error) => {
            console.error('3D 모델을 로드하는 중 오류 발생:', error);
            alert('3D 모델 로딩에 실패했습니다. /public/sci-fi_rocket/ 폴더에 모델 파일이 있는지 확인하세요.');
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
        this.scene.add(line);

        this.map = map;
        this.renderer = new THREE.WebGLRenderer({
          canvas: map.getCanvas(),
          context: gl,
          antialias: true,
        });
        this.renderer.autoClear = false;
      },
      render: function (gl, matrix) {
        const m = new THREE.Matrix4().fromArray(matrix);
        const l = new THREE.Matrix4().makeTranslation(
          modelAsMercatorCoordinate.x,
          modelAsMercatorCoordinate.y,
          modelAsMercatorCoordinate.z
        );

        this.camera.projectionMatrix = m.multiply(l);
        this.renderer.resetState();
        this.renderer.render(this.scene, this.camera);
        this.map.triggerRepaint();
      },
    };

    customLayerRef.current = customLayer;

    map.on('load', () => {
      map.addLayer(customLayer);
      /* map.addSource('trajectory', {
        type: 'geojson',
        lineMetrics: true,
        data: {
          type: 'Feature',
          properties: {},
          geometry: {
            type: 'LineString',
            coordinates: [],
          },
        },
      }); */

      /* map.addLayer({
        id: 'trajectory-line',
        type: 'line',
        source: 'trajectory',
        layout: {
          'line-join': 'round',
          'line-cap': 'round',
        },
        paint: {
          'line-gradient': [
            'interpolate',
            ['linear'],
            ['line-progress'],
            0,
            '#FF0000',
            1,
            '#FF0000',
          ],
          'line-width': 3,
        },
      }); */
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
      resizeObserver.disconnect();
      map.remove();
      mapRef.current = null;
    };
  }, []);

  // 텔레메트리 데이터 변경 시 모델 위치 및 방향 업데이트
  useEffect(() => {
    if (mapRef.current && modelRef.current && lineRef.current) {
        const map = mapRef.current;
        const model = modelRef.current;
        const line = lineRef.current;

        // 1. 새 텔레메트리 데이터의 월드 좌표(메르카토르) 계산
        const currentMercator = mapboxgl.MercatorCoordinate.fromLngLat(
            { lng: telemetry.longitude, lat: telemetry.latitude },
            telemetry.altitude
        );

        // 2. Scene 원점을 기준으로 한 상대 좌표 계산
        const relativePosition = new THREE.Vector3(
          currentMercator.x - modelAsMercatorCoordinate.x,
          currentMercator.y - modelAsMercatorCoordinate.y,
          currentMercator.z - modelAsMercatorCoordinate.z
        );

        // 3. 모델 위치 업데이트
        model.position.copy(relativePosition);

        // 4. 모델 방향 업데이트
        model.rotation.set(
            telemetry.pitch * (Math.PI / 180) + Math.PI / 2, // 기본 X축 90도 회전 + pitch
            telemetry.roll * (Math.PI / 180),
            telemetry.yaw * (Math.PI / 180)
        );
        
        // 5. 궤적 업데이트 (미리 할당된 버퍼 사용)
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

        // 6. 카메라 따라가기
        map.flyTo({
            center: [telemetry.longitude, telemetry.latitude],
            speed: 0.8,
            curve: 1,
            essential: true,
        });
    }
  }, [telemetry]);

  /* // 궤적 데이터 변경 시 Mapbox Source 업데이트
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
  }, [trajectory]); */

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
