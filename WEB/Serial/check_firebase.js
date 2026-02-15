
const { initializeApp } = require("firebase/app");
const { getFirestore, collection, getDocs, query, limit, addDoc, serverTimestamp } = require("firebase/firestore");
require('dotenv').config({ path: '../Client/.env' });

const firebaseConfig = {
    apiKey: process.env.VITE_FIREBASE_API_KEY,
    authDomain: process.env.VITE_FIREBASE_AUTH_DOMAIN,
    projectId: process.env.VITE_FIREBASE_PROJECT_ID,
    storageBucket: process.env.VITE_FIREBASE_STORAGE_BUCKET,
    messagingSenderId: process.env.VITE_FIREBASE_MESSAGING_SENDER_ID,
    appId: process.env.VITE_FIREBASE_APP_ID,
};

const app = initializeApp(firebaseConfig);
const db = getFirestore(app);

async function checkData() {
    console.log("Checking Firestore for project:", process.env.VITE_FIREBASE_PROJECT_ID);

    try {
        console.log("\n--- Attempting Test Write ---");
        const testDoc = await addDoc(collection(db, "launches"), {
            name: "Debug Test Launch",
            date: new Date().toISOString(),
            status: "success",
            maxAltitude: 100,
            maxSpeed: 10,
            duration: 5,
            launchSite: "Debug Studio",
            telemetryData: [{ altitude: 0 }, { altitude: 100 }],
            isTest: true
        });
        console.log("✅ Test Write SUCCESS! Doc ID:", testDoc.id);

        console.log("\n--- Checking 'live' collection (all documents) ---");
        const liveSnapshot = await getDocs(collection(db, "live"));
        if (liveSnapshot.size === 0) {
            console.log("Collection 'live' is EMPTY!");
        }
        liveSnapshot.forEach(doc => {
            console.log(`- Document ID: ${doc.id}`);
            console.log("  Data:", JSON.stringify(doc.data(), null, 2));
        });

        console.log("\n--- Checking 'launches' collection ---");
        const launchesSnapshot = await getDocs(query(collection(db, "launches"), limit(5)));
        console.log("Found", launchesSnapshot.size, "documents in 'launches'");
        launchesSnapshot.forEach(doc => {
            const data = doc.data();
            console.log(`- ID: ${doc.id}, Name: ${data.name}, Date: ${data.date}`);
        });

    } catch (e) {
        console.error("❌ Firestore Error:", e.message);
        if (e.message.includes("PERMISSION_DENIED")) {
            console.log("Tip: Check Firestore API status and Project ID in Firebase Console.");
        }
    }
}

checkData();
