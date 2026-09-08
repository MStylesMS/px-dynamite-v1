(function () {
    const KEY_BASE = "px.api.base";
    const KEY_DEMO = "px.demo.mode";
    const KEY_CFG = "px.dynamite.demo.config.v4";
    const KEY_OVERLAY = "px.dynamite.overlay";
    const KEY_MQTT = "px.dynamite.mqttCode";
    const KEY_MQTT_WEIGHT = "px.dynamite.mqttWeight";
    const KEY_WEIGHT = "px.dynamite.weightSetting";
    const KEY_KEYHOLD = "px.dynamite.demo.keyhold.v1";

    const KEYPAD = [
        ["1", "2", "3", "A"],
        ["4", "5", "6", "B"],
        ["7", "8", "9", "C"],
        ["*", "0", "#", "D"]
    ];
    const CODE_LABELS = "ABCDEFGH";

    function parseGrams(raw) {
        if (raw == null || raw === "") {
            return 0;
        }
        let n = Number(raw);
        if (!Number.isFinite(n) || n < 0) {
            return 0;
        }
        if (n > 0 && n < 10 && !Number.isInteger(n)) {
            n = n * 1000;
        }
        return Math.round(n);
    }

    function defaultChargeWeights() {
        return {
            1: 150, 2: 500, 3: 200, 4: 300,
            5: 250, 6: 350, 7: 250, 8: 400,
            9: 450, 10: 180, 11: 220, 12: 280,
            13: 320, 14: 380, 15: 550
        };
    }

    function normalizeChargeWeights(raw) {
        const defaults = defaultChargeWeights();
        const src = raw && typeof raw === "object" ? raw : {};
        const out = {};
        Object.keys(defaults).forEach((k) => {
            const n = parseGrams(src[k] != null ? src[k] : defaults[k]);
            out[k] = n > 0 ? n : defaults[k];
        });
        return out;
    }

    function chargeWeight(id) {
        const table = (demoConfig && demoConfig.chargeWeights) || defaultChargeWeights();
        return parseGrams(table[id]);
    }

    function formatWeight(n) {
        const g = parseGrams(n);
        return g ? g + " g" : "—";
    }

    function defaultTargetWeights() {
        return [
            { label: "A", grams: 1450 },
            { label: "B", grams: 1450 },
            { label: "C", grams: 1350 },
            { label: "D", grams: 1340 },
            { label: "E", grams: 0 },
            { label: "F", grams: 0 },
            { label: "G", grams: 0 },
            { label: "H", grams: 0 }
        ];
    }

    function gramsFromLegacyIds(ids) {
        return (ids || []).reduce((acc, id) => acc + chargeWeight(id), 0);
    }

    function normalizeTargetWeights(raw, fallbackCodes) {
        const defaults = defaultTargetWeights();
        const src = Array.isArray(raw) ? raw : [];
        const codes = Array.isArray(fallbackCodes) ? fallbackCodes : [];
        return defaults.map((d, i) => {
            const s = src[i] || {};
            let grams = 0;
            if (s.grams != null && s.grams !== "") {
                grams = parseGrams(s.grams);
            } else if (s.ids) {
                grams = gramsFromLegacyIds(parseIds(s.ids));
            } else if (codes[i] && codes[i].ids) {
                grams = gramsFromLegacyIds(parseIds(codes[i].ids));
            } else {
                grams = d.grams;
            }
            return { label: d.label, grams: grams };
        });
    }

    let solutionCountCache = { key: "", map: {} };

    function solutionCountMap() {
        const table = (demoConfig && demoConfig.chargeWeights) || defaultChargeWeights();
        const key = JSON.stringify(table);
        if (solutionCountCache.key === key) {
            return solutionCountCache.map;
        }
        const w = [];
        for (let id = 1; id <= 15; id++) {
            w[id] = parseGrams(table[id]);
        }
        const map = {};
        for (let a = 1; a <= 15; a++) {
            for (let b = a + 1; b <= 15; b++) {
                for (let c = b + 1; c <= 15; c++) {
                    for (let d = c + 1; d <= 15; d++) {
                        const sum = w[a] + w[b] + w[c] + w[d];
                        map[sum] = (map[sum] || 0) + 1;
                    }
                }
            }
        }
        solutionCountCache = { key: key, map: map };
        return map;
    }

    function countSolutions(grams) {
        const g = parseGrams(grams);
        if (!g) {
            return 0;
        }
        return solutionCountMap()[g] || 0;
    }

    function parseIds(raw) {
        const src = Array.isArray(raw) ? raw : String(raw == null ? "" : raw).split(/[,\s]+/);
        const ids = [];
        src.forEach((n) => {
            const v = parseInt(n, 10);
            if (v > 0 && v < 16 && ids.indexOf(v) < 0) {
                ids.push(v);
            }
        });
        return ids.slice(0, 4);
    }

    function defaultCodes() {
        return [
            { label: "A", code: "A7D36#", ids: [7, 2, 4, 8] },
            { label: "B", code: "123456#", ids: [7, 2, 4, 8] },
            { label: "C", code: "B6540", ids: [1, 2, 4, 8] },
            { label: "D", code: "C9870", ids: [15, 7, 11, 13] },
            { label: "E", code: "", ids: [] },
            { label: "F", code: "", ids: [] },
            { label: "G", code: "", ids: [] },
            { label: "H", code: "", ids: [] }
        ];
    }

    function sanitizeCode(value) {
        return String(value == null ? "" : value).toUpperCase().replace(/[^0-9A-D*#]/g, "").slice(0, 16);
    }

    function normalizeCodes(raw) {
        const src = Array.isArray(raw) ? raw : [];
        const defaults = defaultCodes();
        return defaults.map((d, i) => {
            const s = src[i] || {};
            const label = String(s.label || d.label).slice(0, 1).toUpperCase() || d.label;
            const code = sanitizeCode(s.code != null ? s.code : d.code);
            const ids = parseIds(s.ids != null ? s.ids : d.ids);
            return { label: label, code: code, ids: ids };
        });
    }

    function normalizeMode(value) {
        return value === "code_entry" ? "code_entry" : "io";
    }

    const DEFAULT_CONFIG = {
        gameMode: "code_entry",
        gameSetting: "B",
        weightSetting: "B",
        keypadScanMs: 5,
        keypadDebounce: 3,
        reedDebounceMs: 25,
        maglockPulseMs: 250,
        heartbeatInterval: 10000,
        debug: true,
        codes: defaultCodes(),
        chargeWeights: defaultChargeWeights(),
        targetWeights: defaultTargetWeights()
    };

    const SCENARIOS = ["empty", "partial", "ready", "door-open", "send", "no-pressure", "pulse", "io", "mismatch", "mqtt"];

    function el(id) {
        return document.getElementById(id);
    }

    function nowIso() {
        return new Date().toISOString();
    }

    function isServedOverHttp() {
        return window.location.protocol === "http:" || window.location.protocol === "https:";
    }

    function getApiBase() {
        if (isServedOverHttp()) {
            return window.location.origin;
        }
        return localStorage.getItem(KEY_BASE) || "http://192.168.4.1";
    }

    function queryParam(name) {
        try {
            return new URLSearchParams(window.location.search).get(name);
        } catch {
            return null;
        }
    }

    function queryDemoOverride() {
        const q = queryParam("demo");
        if (q === "1" || q === "true") {
            return true;
        }
        if (q === "0" || q === "false") {
            return false;
        }
        return null;
    }

    function isLocalPreviewHost() {
        const h = window.location.hostname;
        return h === "127.0.0.1" || h === "localhost" || h === "[::1]";
    }

    function getDemoMode() {
        const q = queryDemoOverride();
        if (q !== null) {
            return q;
        }
        if (isLocalPreviewHost()) {
            return true;
        }
        return localStorage.getItem(KEY_DEMO) === "1";
    }

    function getScenario() {
        const s = (queryParam("scenario") || "ready").toLowerCase();
        return SCENARIOS.indexOf(s) >= 0 ? s : "ready";
    }

    function normalizeApiPath(path) {
        return path.startsWith("/") ? path.slice(1) : path;
    }

    function resolveApiUrl(path) {
        const rel = normalizeApiPath(path);
        if (!isServedOverHttp()) {
            return getApiBase().replace(/\/$/, "") + "/" + rel;
        }
        try {
            return new URL(rel, document.baseURI || window.location.href).href;
        } catch {
            return "/" + rel;
        }
    }

    function loadDemoConfig() {
        let raw = {};
        try {
            raw = JSON.parse(localStorage.getItem(KEY_CFG) || "{}");
        } catch {
            raw = {};
        }
        return {
            gameMode: normalizeMode(raw.gameMode || DEFAULT_CONFIG.gameMode),
            gameSetting: String(raw.gameSetting || DEFAULT_CONFIG.gameSetting).slice(0, 1).toUpperCase(),
            weightSetting: String(raw.weightSetting || DEFAULT_CONFIG.weightSetting).slice(0, 1).toUpperCase(),
            keypadScanMs: Number(raw.keypadScanMs) || DEFAULT_CONFIG.keypadScanMs,
            keypadDebounce: Number(raw.keypadDebounce) || DEFAULT_CONFIG.keypadDebounce,
            reedDebounceMs: Number(raw.reedDebounceMs) || DEFAULT_CONFIG.reedDebounceMs,
            maglockPulseMs: Number(raw.maglockPulseMs) || DEFAULT_CONFIG.maglockPulseMs,
            heartbeatInterval: Number(raw.heartbeatInterval) || DEFAULT_CONFIG.heartbeatInterval,
            debug: raw.debug != null ? Boolean(raw.debug) : true,
            codes: normalizeCodes(raw.codes),
            chargeWeights: normalizeChargeWeights(raw.chargeWeights),
            targetWeights: normalizeTargetWeights(raw.targetWeights, raw.codes)
        };
    }

    function saveDemoConfig(cfg) {
        demoConfig = {
            gameMode: normalizeMode(cfg.gameMode),
            gameSetting: String(cfg.gameSetting || "B").slice(0, 1).toUpperCase(),
            weightSetting: String(cfg.weightSetting || demoWeightSetting || "B").slice(0, 1).toUpperCase(),
            keypadScanMs: Math.max(1, Number(cfg.keypadScanMs) || 5),
            keypadDebounce: Math.max(1, Number(cfg.keypadDebounce) || 3),
            reedDebounceMs: Math.max(5, Number(cfg.reedDebounceMs) || 25),
            maglockPulseMs: Math.min(400, Math.max(50, Number(cfg.maglockPulseMs) || 250)),
            heartbeatInterval: Number(cfg.heartbeatInterval) || 10000,
            debug: Boolean(cfg.debug),
            codes: normalizeCodes(cfg.codes),
            chargeWeights: normalizeChargeWeights(cfg.chargeWeights),
            targetWeights: normalizeTargetWeights(cfg.targetWeights, cfg.codes)
        };
        localStorage.setItem(KEY_CFG, JSON.stringify(demoConfig));
    }

    let demoConfig = loadDemoConfig();
    let demoMqttCode = sanitizeCode(localStorage.getItem(KEY_MQTT) || "");
    let demoMqttWeight = parseGrams(localStorage.getItem(KEY_MQTT_WEIGHT) || "");
    let demoWeightSetting = (function initWeight() {
        const sc = queryParam("scenario");
        if (sc) {
            return demoConfig.weightSetting || "B";
        }
        return localStorage.getItem(KEY_WEIGHT) || demoConfig.weightSetting || "B";
    })();
    let demoOverlay = (function initOverlay() {
        const sc = queryParam("scenario");
        if (sc === "mqtt") {
            return "mqtt";
        }
        if (sc) {
            return "";
        }
        return localStorage.getItem(KEY_OVERLAY) || "";
    })();

    function bitsFromId(id) {
        const n = Number(id) || 0;
        return [n & 1, (n >> 1) & 1, (n >> 2) & 1, (n >> 3) & 1];
    }

    function idFromBits(bits) {
        return (bits[0] ? 1 : 0) + (bits[1] ? 2 : 0) + (bits[2] ? 4 : 0) + (bits[3] ? 8 : 0);
    }

    function makeSlot(id) {
        const bits = bitsFromId(id);
        const n = idFromBits(bits);
        return {
            id: n,
            occupied: n > 0,
            bits: bits,
            weight: n > 0 ? chargeWeight(n) : null
        };
    }

    function formatId(id) {
        return "ID" + String(id).padStart(2, "0");
    }

    function emptyKeypad() {
        return KEYPAD.map((row) => row.map(() => false));
    }

    function sanitizeKey(raw) {
        const c = String(raw || "").trim().charAt(0);
        if (!c) {
            return "";
        }
        const up = c === "*" || c === "#" ? c : c.toUpperCase();
        return /[0-9A-D*#]/.test(up) ? up : "";
    }

    function keyPos(k) {
        for (let r = 0; r < KEYPAD.length; r++) {
            const c = KEYPAD[r].indexOf(k);
            if (c >= 0) {
                return { r, c };
            }
        }
        return null;
    }

    function isKeyDown(state, k) {
        const pos = keyPos(k);
        if (!pos || !state || !state.keypadDown) {
            return false;
        }
        return Boolean(state.keypadDown[pos.r] && state.keypadDown[pos.r][pos.c]);
    }

    function codeForSetting(setting, codes) {
        const list = codes || demoConfig.codes || [];
        const hit = list.find((c) => c.label === setting);
        return hit && hit.code ? hit.code : "";
    }

    function gramsForWeightSetting(setting) {
        const hit = (demoConfig.targetWeights || []).find((c) => c.label === setting);
        return hit ? parseGrams(hit.grams) : 0;
    }

    function resolveTargetWeight(state) {
        const pick = demoWeightSetting || (state && state.weightSetting) || demoConfig.weightSetting || "B";
        if (pick === "mqtt") {
            return parseGrams((state && state.mqttWeight) || demoMqttWeight);
        }
        return gramsForWeightSetting(pick);
    }

    function resolveTarget(state) {
        const codes = (state && state.codes) || demoConfig.codes;
        const mqtt = sanitizeCode((state && state.mqttCode) || demoMqttCode);
        const setting = (state && state.gameSetting) || demoConfig.gameSetting;
        if (demoOverlay === "mqtt") {
            return mqtt;
        }
        if (demoOverlay) {
            return codeForSetting(demoOverlay, codes);
        }
        return codeForSetting(setting, codes);
    }

    function longestPrefixSuffix(seg, target) {
        const t = String(target || "");
        const s = String(seg || "");
        for (let n = s.length; n >= 1; n--) {
            if (t.slice(0, n) === s.slice(-n)) {
                return n;
            }
        }
        return 0;
    }

    function successNeedle(target) {
        const t = String(target || "");
        if (!t) {
            return "";
        }
        return t.charAt(t.length - 1) === "#" ? t : t + "#";
    }

    function colorClasses(entered, target, mode) {
        const s = String(entered || "").slice(-10);
        const classes = Array(s.length).fill("yellow");
        if (mode !== "code_entry") {
            return s.split("").map(() => "plain");
        }
        if (!s.length) {
            return [];
        }
        const needle = successNeedle(target);
        if (needle && s.endsWith(needle)) {
            const start = s.length - needle.length;
            for (let i = 0; i < start; i++) {
                classes[i] = "grey";
            }
            for (let i = start; i < s.length; i++) {
                classes[i] = "green solved";
            }
            return classes;
        }
        let sep = -1;
        for (let i = s.length - 1; i >= 0; i--) {
            if (s[i] === "*" || s[i] === "#") {
                sep = i;
                break;
            }
        }
        let attemptStart = 0;
        if (sep >= 0) {
            for (let i = 0; i < sep; i++) {
                classes[i] = "grey";
            }
            classes[sep] = "yellow";
            attemptStart = sep + 1;
        }
        const attempt = s.slice(attemptStart);
        const n = longestPrefixSuffix(attempt, target);
        for (let i = 0; i < attempt.length; i++) {
            classes[attemptStart + i] = i >= attempt.length - n ? "green" : "yellow";
        }
        return classes;
    }

    function scenarioState(name) {
        const now = Date.now();
        const slots = [0, 0, 0, 0];
        let pressure = false;
        let doorOpen = false;
        let maglock = 0;
        let lastKey = "";
        let lastPulseMs = 0;
        let pulseTimeoutFired = false;
        let drivenRow = 0;
        let entryWindow = "";
        let gameMode = demoConfig.gameMode;
        let mqttCode = demoMqttCode;
        const keypadDown = emptyKeypad();

        if (name === "empty") {
            pressure = false;
        } else if (name === "partial") {
            slots[0] = 7;
            slots[1] = 2;
            pressure = true;
            lastKey = "7";
            entryWindow = "12";
        } else if (name === "ready") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
        } else if (name === "door-open") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
            doorOpen = true;
            lastKey = "#";
            lastPulseMs = 250;
            pulseTimeoutFired = true;
            entryWindow = "123456#";
        } else if (name === "send") {
            slots[0] = 15;
            slots[1] = 7;
            slots[2] = 11;
            slots[3] = 13;
            pressure = true;
            lastKey = "*";
            keypadDown[3][0] = true;
            drivenRow = 3;
            entryWindow = "A7D36#*";
        } else if (name === "no-pressure") {
            slots[0] = 1;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = false;
        } else if (name === "pulse") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
            maglock = 1;
            lastPulseMs = demoConfig.maglockPulseMs;
            pulseTimeoutFired = false;
        } else if (name === "io") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
            gameMode = "io";
            entryWindow = "1299";
        } else if (name === "mismatch") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
            gameMode = "code_entry";
            entryWindow = "12123";
            lastKey = "3";
        } else if (name === "mqtt") {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
            gameMode = "code_entry";
            mqttCode = "123456#";
            entryWindow = "12";
            lastKey = "2";
        } else {
            slots[0] = 7;
            slots[1] = 2;
            slots[2] = 4;
            slots[3] = 8;
            pressure = true;
        }

        return buildState({
            scenario: name,
            slots: slots.map(makeSlot),
            pressure: pressure,
            doorOpen: doorOpen,
            maglock: maglock,
            lastKey: lastKey,
            keypadDown: keypadDown,
            drivenRow: drivenRow,
            lastPulseMs: lastPulseMs,
            pulseTimeoutFired: pulseTimeoutFired,
            spiOk: true,
            entryWindow: entryWindow,
            gameMode: gameMode,
            mqttCode: mqttCode,
            now: now
        });
    }

    function buildState(src) {
        const slots = (src.slots || []).map((s) => makeSlot(s.id != null ? s.id : idFromBits(s.bits || [0, 0, 0, 0])));
        while (slots.length < 4) {
            slots.push(makeSlot(0));
        }
        const allSlots = slots.every((s) => s.occupied);
        const storagePresent = Boolean(src.pressure);
        const allConnected = allSlots && storagePresent;
        const reeds = [];
        slots.forEach((slot, i) => {
            slot.bits.forEach((bit, b) => {
                reeds.push({
                    slot: i,
                    bit: b,
                    pin: 16 + i * 4 + b,
                    low: Boolean(bit)
                });
            });
        });
        const gameMode = normalizeMode(src.gameMode || demoConfig.gameMode);
        const gameSetting = src.gameSetting || demoConfig.gameSetting;
        const mqttCode = src.mqttCode != null ? src.mqttCode : demoMqttCode;
        const weightSetting = src.weightSetting || demoWeightSetting || demoConfig.weightSetting;
        const mqttWeight = parseGrams(src.mqttWeight != null ? src.mqttWeight : demoMqttWeight);
        const draft = {
            mqttCode: mqttCode,
            mqttWeight: mqttWeight,
            gameSetting: gameSetting,
            weightSetting: weightSetting,
            gameMode: gameMode
        };
        const targetCode = gameMode === "code_entry" ? resolveTarget(draft) : "";
        const targetWeight = resolveTargetWeight(draft);
        const occupiedCount = slots.filter((s) => s.occupied).length;
        const currentWeight = slots.reduce((acc, slot) => acc + (slot.occupied ? parseGrams(slot.weight) : 0), 0);
        const weightMatch = occupiedCount === 4 && targetWeight > 0 && currentWeight === targetWeight;
        return {
            scenario: src.scenario || "live",
            slots: slots,
            id0: slots[0].id,
            id1: slots[1].id,
            id2: slots[2].id,
            id3: slots[3].id,
            pressure: storagePresent,
            storagePresent: storagePresent,
            allConnected: allConnected,
            doorOpen: Boolean(src.doorOpen),
            maglock: src.maglock ? 1 : 0,
            lastKey: src.lastKey || "",
            entryWindow: String(src.entryWindow || "").slice(-10),
            keypadDown: src.keypadDown || emptyKeypad(),
            drivenRow: src.drivenRow == null ? 0 : src.drivenRow,
            lastPulseMs: Number(src.lastPulseMs) || 0,
            pulseTimeoutFired: Boolean(src.pulseTimeoutFired),
            spiOk: src.spiOk !== false,
            reeds: reeds,
            gameMode: gameMode,
            gameSetting: gameSetting,
            mqttCode: mqttCode,
            mqttWeight: mqttWeight,
            targetCode: targetCode,
            weightSetting: weightSetting,
            targetWeight: targetWeight,
            currentWeight: currentWeight,
            occupiedCount: occupiedCount,
            weightMatch: weightMatch,
            solutionCount: countSolutions(targetWeight),
            targetWeights: demoConfig.targetWeights,
            chargeWeights: demoConfig.chargeWeights,
            codes: demoConfig.codes,
            wifiConnected: true,
            wifiSsid: "Paradox-TFD-1",
            wifiRssi: -48,
            lastMqttIn: src.lastMqttIn || '{"magLock":1}',
            lastMqttOut: src.lastMqttOut || JSON.stringify({
                id0: slots[0].id,
                id1: slots[1].id,
                id2: slots[2].id,
                id3: slots[3].id,
                allConnected: allConnected,
                doorOpen: Boolean(src.doorOpen)
            }),
            lastMqttAt: src.now || Date.now()
        };
    }

    let demoCache = scenarioState(getScenario());
    let demoOverrides = {};
    let pulseTimer = null;
    let pulseUntil = 0;
    let demoKeyHoldTimer = null;
    let liveKeypressChain = Promise.resolve();
    let lastLiveState = null;

    function enrichLiveState(state) {
        if (!state) {
            return state;
        }
        const out = Object.assign({}, state);
        if (!out.codes || !out.codes.length) {
            out.codes = demoConfig.codes;
        }
        if (!out.targetWeights || !out.targetWeights.length) {
            out.targetWeights = demoConfig.targetWeights;
        }
        out.gameMode = normalizeMode(out.gameMode || demoConfig.gameMode);
        out.targetCode = resolveTarget(out);
        return out;
    }

    function maglockPulseMs() {
        return Math.min(400, Math.max(50, Number(demoConfig.maglockPulseMs) || 250));
    }

    function applyPersistedKeyHold(base) {
        try {
            const raw = localStorage.getItem(KEY_KEYHOLD);
            if (!raw) {
                return;
            }
            const data = JSON.parse(raw);
            if (!data || Date.now() > Number(data.until || 0)) {
                localStorage.removeItem(KEY_KEYHOLD);
                return;
            }
            if (demoOverrides.keypadDown == null && data.keypadDown) {
                base.keypadDown = data.keypadDown;
            }
            if (demoOverrides.lastKey == null && data.lastKey) {
                base.lastKey = data.lastKey;
            }
            if (demoOverrides.entryWindow == null && data.entryWindow != null) {
                base.entryWindow = data.entryWindow;
            }
            if (demoOverrides.drivenRow == null && data.drivenRow != null) {
                base.drivenRow = data.drivenRow;
            }
        } catch {
            /* ignore */
        }
    }

    function currentState() {
        expireMaglockPulse();
        const base = scenarioState(getScenario());
        applyPersistedKeyHold(base);
        Object.keys(demoOverrides).forEach((k) => {
            if (demoOverrides[k] !== undefined) {
                base[k] = demoOverrides[k];
            }
        });
        if (demoOverrides.slots) {
            base.slots = demoOverrides.slots;
        }
        demoCache = buildState(base);
        return demoCache;
    }

    async function api(path, options) {
        if (getDemoMode()) {
            return mockResponse(path, options);
        }
        const requestUrl = resolveApiUrl(path);
        const res = await fetch(requestUrl, options);
        const rawText = await res.text();
        let data = null;
        if (rawText) {
            try {
                data = JSON.parse(rawText);
            } catch {
                data = null;
            }
        }
        if (!res.ok) {
            throw new Error("HTTP " + res.status + " " + res.statusText);
        }
        return data;
    }

    function expireMaglockPulse() {
        if (!pulseUntil) {
            return;
        }
        if (Date.now() < pulseUntil) {
            demoOverrides.maglock = 1;
            return;
        }
        pulseUntil = 0;
        demoOverrides.maglock = 0;
        demoOverrides.pulseTimeoutFired = true;
        demoOverrides.lastMqttIn = '{"magLock":0}';
        if (pulseTimer) {
            clearTimeout(pulseTimer);
            pulseTimer = null;
        }
    }

    function refreshLiveIfVisible() {
        if (el("statusPills") || el("sendBay") || el("liveKeypad")) {
            setLive(currentState());
        }
    }

    function applyDemoKeypress(raw) {
        const k = sanitizeKey(raw);
        if (!k) {
            return;
        }
        const prev = demoOverrides.entryWindow != null ? demoOverrides.entryWindow : currentState().entryWindow;
        const down = emptyKeypad();
        const pos = keyPos(k);
        demoOverrides.lastKey = k;
        demoOverrides.entryWindow = (String(prev || "") + k).slice(-10);
        if (pos) {
            down[pos.r][pos.c] = true;
            demoOverrides.drivenRow = pos.r;
        }
        demoOverrides.keypadDown = down;
        demoOverrides.lastMqttOut = JSON.stringify({ keypress: k });
        try {
            localStorage.setItem(KEY_KEYHOLD, JSON.stringify({
                keypadDown: down,
                lastKey: k,
                entryWindow: demoOverrides.entryWindow,
                drivenRow: demoOverrides.drivenRow,
                until: Date.now() + 2500
            }));
        } catch {
            /* ignore */
        }
        if (demoKeyHoldTimer) {
            clearTimeout(demoKeyHoldTimer);
        }
        demoKeyHoldTimer = setTimeout(() => {
            demoOverrides.keypadDown = emptyKeypad();
            try {
                localStorage.removeItem(KEY_KEYHOLD);
            } catch {
                /* ignore */
            }
            refreshLiveIfVisible();
        }, 2500);
    }

    function startDemoPulse() {
        const ms = maglockPulseMs();
        pulseUntil = Date.now() + ms;
        demoOverrides.maglock = 1;
        demoOverrides.lastPulseMs = ms;
        demoOverrides.pulseTimeoutFired = false;
        demoOverrides.lastMqttIn = '{"magLock":1}';
        if (pulseTimer) {
            clearTimeout(pulseTimer);
        }
        pulseTimer = setTimeout(() => {
            pulseUntil = 0;
            demoOverrides.maglock = 0;
            demoOverrides.pulseTimeoutFired = true;
            demoOverrides.lastMqttIn = '{"magLock":0}';
            pulseTimer = null;
            refreshLiveIfVisible();
        }, ms);
    }

    function dropMaglock() {
        pulseUntil = 0;
        demoOverrides.maglock = 0;
        demoOverrides.lastMqttIn = '{"magLock":0}';
        if (pulseTimer) {
            clearTimeout(pulseTimer);
            pulseTimer = null;
        }
    }

    function applyTargetPayload(payload) {
        const code = sanitizeCode(payload.targetCode || payload.TargetCode || payload.Code || payload.code || "");
        let grams = parseGrams(payload.targetWeight != null ? payload.targetWeight : payload.weight);
        if (!grams) {
            const ids = parseIds(payload.ids || payload.Ids || payload.solutionIds);
            if (ids.length) {
                grams = ids.reduce((acc, id) => acc + chargeWeight(id), 0);
            }
        }
        if (!code && !grams) {
            return false;
        }
        if (code) {
            demoMqttCode = code;
            demoOverlay = "mqtt";
            localStorage.setItem(KEY_MQTT, demoMqttCode);
            localStorage.setItem(KEY_OVERLAY, "mqtt");
            demoOverrides.mqttCode = demoMqttCode;
            demoOverrides.gameMode = "code_entry";
        }
        if (grams > 0) {
            demoMqttWeight = grams;
            demoWeightSetting = "mqtt";
            localStorage.setItem(KEY_MQTT_WEIGHT, String(demoMqttWeight));
            localStorage.setItem(KEY_WEIGHT, "mqtt");
            demoOverrides.mqttWeight = demoMqttWeight;
            demoOverrides.weightSetting = "mqtt";
        }
        demoOverrides.lastMqttIn = JSON.stringify({
            targetCode: demoMqttCode,
            targetWeight: demoMqttWeight || undefined
        });
        demoOverrides.lastMqttAt = Date.now();
        return true;
    }

    async function mockResponse(path, options) {
        await new Promise((r) => setTimeout(r, 40));
        const payload = options && options.body ? JSON.parse(options.body) : {};
        const cmd = payload.Command || payload.command;

        if (path === "/api/state" || path === "/api/monitor") {
            return currentState();
        }
        if (path === "/api/config/defaults") {
            return JSON.parse(JSON.stringify(DEFAULT_CONFIG));
        }
        if (path === "/api/config" && options && options.method === "POST") {
            saveDemoConfig(payload);
            return { ok: true, mock: true };
        }
        if (path === "/api/config") {
            return JSON.parse(JSON.stringify(demoConfig));
        }
        if (path === "/api/config/save") {
            saveDemoConfig(payload);
            return { ok: true, mock: true, persisted: true };
        }
        if (path === "/api/command") {
            if (payload.magLock === 1 || cmd === "unlockCabinet" || cmd === "openDoor") {
                startDemoPulse();
            } else if (payload.magLock === 0) {
                dropMaglock();
            } else if (cmd === "reportState") {
                demoOverrides.lastMqttOut = JSON.stringify({
                    id0: currentState().id0,
                    id1: currentState().id1,
                    id2: currentState().id2,
                    id3: currentState().id3,
                    allConnected: currentState().allConnected,
                    doorOpen: currentState().doorOpen
                });
            } else if (payload.doorOpen === true || payload.doorOpen === false) {
                demoOverrides.doorOpen = Boolean(payload.doorOpen);
            } else if (cmd === "setTarget" || payload.targetCode || payload.Code || payload.targetWeight != null) {
                applyTargetPayload(payload);
            } else if (payload.keypress) {
                applyDemoKeypress(payload.keypress);
            }
            return { ok: true, mock: true, received: payload };
        }
        if (path === "/api/connection") {
            if (options && options.method === "POST") {
                return { ok: true, mock: true, applied: true };
            }
            return {
                wifiSsid: "Paradox-TFD-1",
                wifiPassword: "",
                mqttHost: "192.168.8.132",
                mqttPort: 1883,
                mqttUsername: "",
                mqttPassword: "",
                mqttBaseTopic: "/Paradox/ParadoxDynamiteProp",
                mqttCommandTopic: "/Paradox/ParadoxDynamiteProp/command",
                mqttStateTopic: "/Paradox/ParadoxDynamiteProp/state",
                mqttEventsTopic: "/Paradox/ParadoxDynamiteProp/state",
                mqttWarningsTopic: "paradox/tfd/dynamite/warnings",
                mqttGameStateTopic: "paradox/tfd/state",
                mqttPropAnnounceTopic: "/Paradox/Props",
                networkName: "dynamite",
                apSsid: "Paradox-PXDynamiteV1-A1B2",
                apIpAddress: "192.168.4.1",
                apPassword: "",
                apEnabled: true
            };
        }
        if (path === "/api/connection/scan") {
            return {
                ok: true,
                networks: [
                    { ssid: "Paradox-TFD-1", rssi: -42 },
                    { ssid: "Paradox-TFD-2", rssi: -61 },
                    { ssid: "Props-Backstage", rssi: -70 }
                ]
            };
        }
        if (path === "/api/details") {
            return {
                propName: "Dynamite32Prop",
                ipAddress: "(offline / demo)",
                softwareVersion: "0.01-demo",
                buildNumber: "demo",
                buildDate: "2026-09-04",
                cpuTemp: "39 C",
                freeMemory: "188 KB"
            };
        }
        return { ok: true, mock: true };
    }

    function appendLog(node, value) {
        if (!node) {
            return;
        }
        node.textContent = "[" + nowIso() + "]\n" + JSON.stringify(value, null, 2) + "\n\n" + node.textContent;
    }

    function tablerWifiSvg(level) {
        const l = Math.max(0, Math.min(4, Number(level || 0)));
        const color = l >= 3 ? "#00c45c" : l === 2 ? "#f0a92a" : "#ef4444";
        const op1 = l >= 1 ? 1 : 0.25;
        const op2 = l >= 2 ? 1 : 0.25;
        const op3 = l >= 3 ? 1 : 0.25;
        const op4 = l >= 4 ? 1 : 0.25;
        return `<svg class="wifi-svg" viewBox="0 0 24 24" aria-hidden="true"><path d="M3 9.5a13 13 0 0 1 18 0" fill="none" stroke="${color}" stroke-opacity="${op4}" stroke-width="1.8" stroke-linecap="round"/><path d="M6 13a9 9 0 0 1 12 0" fill="none" stroke="${color}" stroke-opacity="${op3}" stroke-width="1.8" stroke-linecap="round"/><path d="M9 16.5a5 5 0 0 1 6 0" fill="none" stroke="${color}" stroke-opacity="${op2}" stroke-width="1.8" stroke-linecap="round"/><circle cx="12" cy="20" r="1.5" fill="${color}" fill-opacity="${op1}"/></svg>`;
    }

    function rssiLevel(rssi) {
        if (rssi >= -50) {
            return 4;
        }
        if (rssi >= -60) {
            return 3;
        }
        if (rssi >= -70) {
            return 2;
        }
        return 1;
    }

    function ensureWifiBadge() {
        let badge = el("wifiBadge");
        if (badge) {
            return badge;
        }
        const container = el("statusIcons");
        if (!container) {
            return null;
        }
        badge = document.createElement("div");
        badge.id = "wifiBadge";
        badge.className = "wifi-badge";
        badge.innerHTML = `<span id="wifiBadgeIcon" class="wifi-icon"></span><span id="wifiBadgeText">--</span>`;
        container.insertBefore(badge, container.firstChild);
        return badge;
    }

    function ensureHwBanner() {
        let bar = el("hwBanner");
        if (bar) {
            return bar;
        }
        bar = document.createElement("div");
        bar.id = "hwBanner";
        bar.className = "hw-banner hidden";
        bar.setAttribute("role", "alert");
        document.body.insertBefore(bar, document.body.firstChild);
        return bar;
    }

    function renderHwBanner(details) {
        const bar = ensureHwBanner();
        const msg = details && (details.hwFault || details.hardwareError);
        if (msg) {
            bar.textContent = msg;
            bar.classList.remove("hidden");
        } else {
            bar.textContent = "";
            bar.classList.add("hidden");
        }
    }

    function renderWifiStatus(details) {
        renderHwBanner(details);
        const badge = ensureWifiBadge();
        if (!badge) {
            return;
        }
        const icon = el("wifiBadgeIcon");
        const text = el("wifiBadgeText");
        if (details && details.wifiConnected) {
            if (icon) {
                icon.innerHTML = tablerWifiSvg(rssiLevel(details.wifiRssi));
            }
            if (text) {
                text.textContent = details.wifiSsid || "WiFi";
            }
        } else if (icon && text) {
            icon.innerHTML = tablerWifiSvg(0);
            text.textContent = "off";
        }
    }

    async function fetchStatusIcons() {
        try {
            const state = await api("/api/state");
            renderWifiStatus(state);
            renderHwBanner(state);
        } catch {
            renderWifiStatus(null);
            renderHwBanner(null);
        }
    }

    function chargeSvg(occupied, tone) {
        let body = "none";
        let edge = "#6a849e";
        let cap = "none";
        let flange = "none";
        let dash = " stroke-dasharray=\"3 3\"";
        if (occupied && tone === "ok") {
            body = "#3dcc7a";
            edge = "#1a7a44";
            cap = "#9aecc0";
            flange = "#2ea35f";
            dash = "";
        } else if (occupied && tone === "bad") {
            body = "#e05555";
            edge = "#8a2020";
            cap = "#f0a0a0";
            flange = "#c43d3d";
            dash = "";
        } else if (occupied) {
            body = "#c8ccd0";
            edge = "#6f767e";
            cap = "#e4e7ea";
            flange = "#b4b8be";
            dash = "";
        }
        return `<svg viewBox="0 0 120 128" aria-hidden="true">
            <ellipse cx="60" cy="118" rx="46" ry="7" fill="${flange}" stroke="${edge}" stroke-width="2"${dash}/>
            <rect x="18" y="34" width="84" height="84" rx="6" fill="${body}" stroke="${edge}" stroke-width="2.2"${dash}/>
            <path d="M18 36 L102 36 L90 12 L30 12 Z" fill="${cap}" stroke="${edge}" stroke-width="2"${dash}/>
            <ellipse cx="60" cy="12" rx="30" ry="5" fill="${cap}" stroke="${edge}" stroke-width="1.8"${dash}/>
        </svg>`;
    }

    function chargeTone(state, slot) {
        if (!slot.occupied) {
            return "empty";
        }
        if (!state || state.occupiedCount < 4) {
            return "idle";
        }
        return state.weightMatch ? "ok" : "bad";
    }

    function renderCharge(slot, opts) {
        const tone = chargeTone(opts.state, slot);
        const d = document.createElement("div");
        d.className = "charge" + (slot.occupied ? " in" : "") + (tone === "ok" || tone === "bad" ? " " + tone : "");
        const info = (opts.showInfo && slot.occupied)
            ? `<div class="charge-info"><span class="w">${formatWeight(slot.weight)}</span><span class="i">${formatId(slot.id)}</span></div>`
            : "";
        const label = opts.slotLabel
            ? `<div class="charge-slot">${opts.slotLabel}</div>`
            : "";
        d.innerHTML = `<div class="charge-body">${chargeSvg(slot.occupied, tone)}${info}</div>${label}`;
        return d;
    }

    function doorSvg(open) {
        if (open) {
            return `<svg viewBox="0 0 170 130" role="img" aria-label="Door open">
                <rect x="58" y="14" width="100" height="100" rx="3" fill="none" stroke="currentColor" stroke-width="2.4"/>
                <path d="M58 14 L46 26 V116 L58 114" fill="none" stroke="currentColor" stroke-width="1.8"/>
                <rect x="64" y="22" width="88" height="84" rx="2" fill="rgba(6,10,16,0.72)" stroke="currentColor" stroke-width="1.2"/>
                <path d="M58 22 L16 32 L16 118 L58 106 Z" fill="rgba(38,92,88,0.55)" stroke="currentColor" stroke-width="2"/>
                <circle cx="28" cy="68" r="2.6" fill="#e8f0ff"/>
                <circle cx="28" cy="82" r="2.6" fill="#e8f0ff"/>
            </svg>`;
        }
        return `<svg viewBox="0 0 130 130" role="img" aria-label="Door closed">
            <rect x="16" y="14" width="98" height="100" rx="3" fill="none" stroke="currentColor" stroke-width="2.4"/>
            <rect x="22" y="20" width="86" height="88" rx="2" fill="rgba(38,92,88,0.42)" stroke="currentColor" stroke-width="1.8"/>
            <circle cx="96" cy="60" r="2.6" fill="#e8f0ff"/>
            <circle cx="96" cy="74" r="2.6" fill="#e8f0ff"/>
        </svg>`;
    }

    function isSym(ch) {
        return ch === "*" || ch === "#";
    }

    function renderEntry(state) {
        const strip = el("entryStrip");
        if (!strip) {
            return;
        }
        const raw = String(state.entryWindow || "").slice(-10);
        const colors = colorClasses(raw, state.targetCode, state.gameMode);
        const cells = [];
        for (let i = 0; i < 10 - raw.length; i++) {
            cells.push({ ch: "", cls: "empty" });
        }
        raw.split("").forEach((ch, i) => {
            cells.push({ ch: ch, cls: colors[i] || "plain" });
        });
        strip.innerHTML = cells.map((c) => {
            const shown = c.ch ? c.ch : "8";
            const extra = !c.ch ? " empty" : (isSym(c.ch) ? " sym" : "");
            return `<span class="entry-cell ${c.cls}${extra}">${shown}</span>`;
        }).join("");
    }

    function fillCodeSelect(state) {
        const sel = el("targetCodeSelect");
        const wrap = el("codeSelectWrap");
        if (!sel || !wrap) {
            return;
        }
        const codeMode = state.gameMode === "code_entry";
        wrap.classList.toggle("hidden", !codeMode);
        if (!codeMode) {
            return;
        }
        const mqtt = state.mqttCode || demoMqttCode;
        const prev = demoOverlay || state.gameSetting;
        const next = [];
        if (mqtt) {
            next.push({ value: "mqtt", text: "MQTT — " + mqtt });
        }
        (state.codes || demoConfig.codes || []).forEach((c) => {
            next.push({
                value: c.label,
                text: c.label + "-" + (c.code || "(empty)")
            });
        });
        const sig = next.map((o) => o.value + ":" + o.text).join("|");
        if (sel.dataset.sig !== sig) {
            sel.dataset.sig = sig;
            sel.innerHTML = "";
            next.forEach((item) => {
                const o = document.createElement("option");
                o.value = item.value;
                o.textContent = item.text;
                sel.appendChild(o);
            });
        }
        if ([].some.call(sel.options, (o) => o.value === prev)) {
            sel.value = prev;
        } else if (sel.options.length) {
            sel.value = sel.options[0].value;
        }
    }

    function fillWeightSelect(state) {
        const sel = el("targetWeightSelect");
        if (!sel) {
            return;
        }
        const mqttWeight = parseGrams(state.mqttWeight != null ? state.mqttWeight : demoMqttWeight);
        const prev = demoWeightSetting || state.weightSetting || "B";
        const next = [];
        if (mqttWeight > 0) {
            next.push({
                value: "mqtt",
                text: "MQTT — " + formatWeight(mqttWeight)
            });
        }
        (state.targetWeights || demoConfig.targetWeights || []).forEach((w) => {
            const grams = parseGrams(w.grams);
            if (!grams) {
                next.push({ value: w.label, text: w.label + " — (empty)" });
                return;
            }
            const n = countSolutions(grams);
            next.push({
                value: w.label,
                text: w.label + " — " + formatWeight(grams) + (n ? " · " + n + (n === 1 ? " solution" : " solutions") : " · invalid")
            });
        });
        const sig = next.map((o) => o.value + ":" + o.text).join("|");
        if (sel.dataset.sig !== sig) {
            sel.dataset.sig = sig;
            sel.innerHTML = "";
            next.forEach((item) => {
                const o = document.createElement("option");
                o.value = item.value;
                o.textContent = item.text;
                sel.appendChild(o);
            });
        }
        if ([].some.call(sel.options, (o) => o.value === prev)) {
            sel.value = prev;
        } else if (sel.options.length) {
            sel.value = sel.options[0].value;
        }
    }

    function setLive(state) {
        if (!state) {
            return;
        }
        state = enrichLiveState(state);
        lastLiveState = state;
        const codeMode = state.gameMode === "code_entry";
        const badge = el("modeBadge");
        if (badge) {
            badge.className = "badge " + (codeMode ? "mode-code" : "mode-io");
            badge.textContent = codeMode ? "Code Entry" : "Basic I/O";
        }
        if (el("modeHint")) {
            el("modeHint").textContent = codeMode
                ? "Code Entry — Send Bay stays grey until all four charges are in, then green if the gram sum matches the target, red if not. Full code+# stays bold green until the next key."
                : "Basic I/O — reeds, keypad, and maglock only. No on-device match.";
        }
        fillCodeSelect(state);
        fillWeightSelect(state);
        if (el("targetWindowWrap")) {
            el("targetWindowWrap").classList.toggle("hidden", !codeMode);
        }
        if (el("targetReadout")) {
            el("targetReadout").textContent = state.targetCode || "—";
        }

        const targetW = el("targetWeightReadout");
        const currentW = el("currentWeightReadout");
        const list = el("targetChargeList");
        const full = state.occupiedCount === 4;
        const matched = Boolean(state.weightMatch);
        if (targetW) {
            targetW.textContent = formatWeight(state.targetWeight);
            targetW.classList.toggle("ok", matched);
        }
        if (currentW) {
            currentW.textContent = formatWeight(state.currentWeight);
            currentW.classList.toggle("ok", matched);
            currentW.classList.toggle("bad", full && !matched && state.targetWeight > 0);
        }
        if (list) {
            if (!state.targetWeight) {
                list.textContent = "No target weight selected.";
            } else if (!state.solutionCount) {
                list.textContent = "No 4-charge combination hits " + formatWeight(state.targetWeight) + ".";
            } else {
                list.textContent = state.solutionCount + (state.solutionCount === 1 ? " unordered 4-charge solution." : " unordered 4-charge solutions.");
            }
        }

        const send = el("sendBay");
        if (send) {
            send.innerHTML = "";
            (state.slots || []).forEach((slot, i) => {
                send.appendChild(renderCharge(slot, {
                    showInfo: true,
                    slotLabel: "Slot " + (i + 1),
                    state: state
                }));
            });
        }
        const store = el("storageBay");
        if (store) {
            store.innerHTML = "";
            for (let i = 0; i < 4; i++) {
                store.appendChild(renderCharge(
                    { occupied: Boolean(state.storagePresent), id: 0, weight: null },
                    { showInfo: false, slotLabel: "", state: null }
                ));
            }
        }

        renderEntry(state);
        if (el("doorDraw")) {
            el("doorDraw").innerHTML = doorSvg(Boolean(state.doorOpen));
        }
        if (el("doorState")) {
            el("doorState").textContent = state.doorOpen ? "OPEN" : "CLOSED";
            el("doorState").classList.toggle("open", Boolean(state.doorOpen));
        }
        if (el("statusPills")) {
            const doorCls = state.doorOpen ? "pill-warn" : "pill-ok";
            const storeCls = state.storagePresent ? "pill-ok" : "pill-bad";
            const mag = state.maglock
                ? `<span class="pill pill-warn">Maglock HIGH</span>`
                : "";
            el("statusPills").innerHTML =
                `<span class="pill ${doorCls}">Door ${state.doorOpen ? "OPEN" : "CLOSED"}</span>` +
                mag +
                `<span class="pill ${storeCls}">Storage ${state.storagePresent ? "present" : "missing"}</span>`;
        }

        const pads = el("liveKeypad");
        if (pads && !pads.dataset.built) {
            pads.dataset.built = "1";
            KEYPAD.forEach((rowKeys) => {
                rowKeys.forEach((k) => {
                    const b = document.createElement("button");
                    b.type = "button";
                    b.className = "key demo-key";
                    b.textContent = k;
                    b.dataset.key = k;
                    b.addEventListener("click", () => {
                        liveKeypressChain = liveKeypressChain.then(async () => {
                            try {
                                await api("/api/command", { method: "POST", body: JSON.stringify({ keypress: k }) });
                                setLive(await api("/api/state"));
                            } catch {
                                /* ignore */
                            }
                        });
                    });
                    pads.appendChild(b);
                });
            });
        }
        if (pads) {
            Array.from(pads.querySelectorAll(".key")).forEach((b) => {
                b.classList.toggle("last", b.dataset.key === state.lastKey);
                b.classList.toggle("down", isKeyDown(state, b.dataset.key));
            });
        }
    }

    function renderReedGrid(state) {
        const grid = el("reedGrid");
        if (!grid) {
            return;
        }
        grid.innerHTML = "<div></div><div class=\"reed-label\">bit0 ×1</div><div class=\"reed-label\">bit1 ×2</div><div class=\"reed-label\">bit2 ×4</div><div class=\"reed-label\">bit3 ×8</div>";
        (state.slots || []).forEach((slot, i) => {
            const lab = document.createElement("div");
            lab.className = "reed-label";
            lab.textContent = "id" + i + " = " + slot.id + (slot.occupied ? "" : " (empty)");
            grid.appendChild(lab);
            slot.bits.forEach((bit, b) => {
                const cell = document.createElement("div");
                cell.className = "reed-slot";
                const pin = 16 + i * 4 + b;
                cell.innerHTML = `<span class="reed-bit${bit ? " low" : ""}">${bit ? "L" : "H"}</span><span class="pin-num">P${pin}</span>`;
                grid.appendChild(cell);
            });
        });
    }

    function renderMonitorFacts(state) {
        const box = el("monitorFacts");
        if (!box) {
            return;
        }
        const items = [
            ["Game mode", state.gameMode === "code_entry" ? "Code Entry" : "Basic I/O"],
            ["Target", state.targetCode || "—"],
            ["Target weight", formatWeight(state.targetWeight)],
            ["Send bay sum", formatWeight(state.currentWeight) + (state.weightMatch ? " (match)" : "")],
            ["Entry window", state.entryWindow || "—"],
            ["Pressure / storage", state.storagePresent ? "present (all 4)" : "missing (all 4)"],
            ["allConnected", String(state.allConnected)],
            ["Door GPIO 33", state.doorOpen ? "OPEN" : "CLOSED"],
            ["Maglock GPIO 23", state.maglock ? "HIGH" : "LOW"],
            ["Last pulse", (state.lastPulseMs || 0) + " ms"],
            ["Self-timeout fired", String(Boolean(state.pulseTimeoutFired))],
            ["SPI HSPI", state.spiOk ? "ok" : "FAULT"],
            ["Last MQTT in", state.lastMqttIn || "—"],
            ["Last MQTT out", state.lastMqttOut || "—"]
        ];
        box.innerHTML = items.map((it) => `<div class="detail-item"><span>${it[0]}</span><strong>${it[1]}</strong></div>`).join("");
        if (el("pulseLog")) {
            el("pulseLog").textContent = state.maglock
                ? "Coil HIGH — firmware will drop after " + (demoConfig.maglockPulseMs || 250) + " ms even if Node misses magLock:0."
                : (state.pulseTimeoutFired ? "Last pulse ended by self-timeout (coil safe)." : "Coil idle.");
        }
    }

    function renderKeypadMatrix(state) {
        const box = el("keypadMatrix");
        if (!box) {
            return;
        }
        box.innerHTML = "";
        KEYPAD.forEach((rowKeys, r) => {
            rowKeys.forEach((k, c) => {
                const d = document.createElement("div");
                const down = state.keypadDown && state.keypadDown[r] && state.keypadDown[r][c];
                d.className = "kcell" + (down ? " low" : "");
                d.textContent = k;
                box.appendChild(d);
            });
        });
    }

    function renderMcp(state) {
        const box = el("mcpGrid");
        if (!box) {
            return;
        }
        function chip(title, rows) {
            return `<div class="mcp-chip"><h3>${title}</h3><table class="pin-table"><thead><tr><th>Pin</th><th>Role</th><th>Lvl</th></tr></thead><tbody>${rows}</tbody></table></div>`;
        }
        const bank1 = [];
        for (let i = 0; i < 4; i++) {
            bank1.push(`<tr><td>${i}</td><td class="pin-out">row ${i}</td><td class="${state.drivenRow === i ? "lvl-low" : "lvl-high"}">${state.drivenRow === i ? "LOW" : "HIGH"}</td></tr>`);
        }
        for (let i = 4; i < 8; i++) {
            const c = i - 4;
            const down = state.keypadDown && state.keypadDown[state.drivenRow] && state.keypadDown[state.drivenRow][c];
            bank1.push(`<tr><td>${i}</td><td>col ${c}</td><td class="${down ? "lvl-low" : "lvl-high"}">${down ? "LOW" : "HIGH"}</td></tr>`);
        }
        bank1.push(`<tr><td>8</td><td>storage / pressure</td><td class="${state.pressure ? "lvl-low" : "lvl-high"}">${state.pressure ? "LOW" : "HIGH"}</td></tr>`);
        const bank2 = (state.reeds || []).map((r) => {
            return `<tr><td>${r.pin}</td><td>send ${r.slot} b${r.bit}</td><td class="${r.low ? "lvl-low" : "lvl-high"}">${r.low ? "LOW" : "HIGH"}</td></tr>`;
        }).join("");
        box.innerHTML = chip("Bank1 addr 0", bank1.join("")) + chip("Bank2 addr 1", bank2);
    }

    function fillForm(form, cfg) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                field.checked = Boolean(cfg[field.name]);
            } else if (cfg[field.name] != null) {
                field.value = cfg[field.name];
            }
        });
    }

    function collectForm(form, body) {
        if (!form) {
            return;
        }
        Array.from(form.elements).forEach((field) => {
            if (!field.name) {
                return;
            }
            if (field.type === "checkbox") {
                body[field.name] = field.checked;
            } else if (field.type === "number") {
                body[field.name] = Number(field.value);
            } else {
                body[field.name] = field.value;
            }
        });
    }

    function fillCodeSets(codes) {
        const box = el("codeSets");
        if (!box) {
            return;
        }
        box.classList.add("code-sets");
        box.innerHTML = "";
        normalizeCodes(codes).forEach((c, i) => {
            const row = document.createElement("div");
            row.className = "target-set-row";
            row.innerHTML = `<span class="target-set-idx">${CODE_LABELS.charAt(i)}</span><label>Code<input data-code-idx="${i}" value="${c.code}" maxlength="16" placeholder="e.g. A7D36#"></label>`;
            box.appendChild(row);
        });
    }

    function collectCodeSets() {
        return Array.from(document.querySelectorAll("[data-code-idx]")).map((input, i) => {
            return {
                label: CODE_LABELS.charAt(i),
                code: sanitizeCode(input.value)
            };
        });
    }

    function refreshWeightSetRow(row) {
        if (!row) {
            return;
        }
        const input = row.querySelector("[data-wgrams]");
        const status = row.querySelector("[data-wstatus]");
        const grams = parseGrams(input && input.value);
        const n = countSolutions(grams);
        if (input) {
            input.classList.toggle("weight-invalid", Boolean(grams) && n === 0);
        }
        if (status) {
            if (!grams) {
                status.textContent = "unused";
                status.className = "weight-status";
            } else if (!n) {
                status.textContent = "no solutions";
                status.className = "weight-status bad";
            } else {
                status.textContent = n === 1 ? "1 solution" : n + " solutions";
                status.className = "weight-status";
            }
        }
    }

    function fillWeightSets(sets) {
        const box = el("weightSets");
        if (!box) {
            return;
        }
        box.innerHTML = "";
        normalizeTargetWeights(sets).forEach((w, i) => {
            const row = document.createElement("div");
            row.className = "weight-set-row";
            const shown = w.grams ? String(w.grams) : "";
            row.innerHTML = `<span class="target-set-idx">${CODE_LABELS.charAt(i)}</span><label>Target (g)<input data-wgrams type="number" min="0" max="5000" step="1" value="${shown}" placeholder="e.g. 1450"></label><p class="weight-status" data-wstatus></p>`;
            box.appendChild(row);
            refreshWeightSetRow(row);
        });
        box.querySelectorAll("[data-wgrams]").forEach((input) => {
            input.addEventListener("input", () => refreshWeightSetRow(input.closest(".weight-set-row")));
        });
    }

    function collectWeightSets() {
        return Array.from(document.querySelectorAll(".weight-set-row")).map((row, i) => {
            const input = row.querySelector("[data-wgrams]");
            return { label: CODE_LABELS.charAt(i), grams: parseGrams(input && input.value) };
        });
    }

    function fillChargeWeights(weights) {
        const box = el("chargeWeights");
        if (!box) {
            return;
        }
        const table = normalizeChargeWeights(weights);
        box.innerHTML = "";
        for (let id = 1; id <= 15; id++) {
            const lab = document.createElement("label");
            lab.innerHTML = `${formatId(id)} (g)<input data-charge-id="${id}" type="number" min="1" max="2000" step="1" value="${table[id]}">`;
            box.appendChild(lab);
        }
        box.querySelectorAll("input").forEach((input) => {
            input.addEventListener("input", () => {
                const live = {};
                box.querySelectorAll("[data-charge-id]").forEach((field) => {
                    live[field.getAttribute("data-charge-id")] = Number(field.value);
                });
                demoConfig.chargeWeights = normalizeChargeWeights(live);
                document.querySelectorAll(".weight-set-row").forEach(refreshWeightSetRow);
            });
        });
    }

    function collectChargeWeights() {
        const out = {};
        document.querySelectorAll("[data-charge-id]").forEach((input) => {
            out[input.getAttribute("data-charge-id")] = Number(input.value);
        });
        return normalizeChargeWeights(out);
    }

    async function pageDashboard() {
        try {
            const cfg = await api("/api/config");
            if (cfg && cfg.codes) {
                demoConfig.codes = normalizeCodes(cfg.codes);
                demoConfig.gameMode = normalizeMode(cfg.gameMode);
                demoConfig.gameSetting = cfg.gameSetting || demoConfig.gameSetting;
                if (cfg.weightSetting) {
                    demoConfig.weightSetting = cfg.weightSetting;
                }
                if (cfg.targetWeights) {
                    demoConfig.targetWeights = normalizeTargetWeights(cfg.targetWeights, cfg.codes);
                }
                if (cfg.chargeWeights) {
                    demoConfig.chargeWeights = normalizeChargeWeights(cfg.chargeWeights);
                }
            }
        } catch {
            /* keep local */
        }

        const sel = el("targetCodeSelect");
        if (sel) {
            sel.addEventListener("change", () => {
                demoOverlay = sel.value;
                localStorage.setItem(KEY_OVERLAY, demoOverlay);
                if (demoOverlay !== "mqtt") {
                    demoConfig.gameSetting = demoOverlay;
                    saveDemoConfig(demoConfig);
                }
                setLive(lastLiveState || currentState());
            });
        }
        const wsel = el("targetWeightSelect");
        if (wsel) {
            wsel.addEventListener("change", () => {
                demoWeightSetting = wsel.value;
                localStorage.setItem(KEY_WEIGHT, demoWeightSetting);
                if (demoWeightSetting !== "mqtt") {
                    demoConfig.weightSetting = demoWeightSetting;
                    saveDemoConfig(demoConfig);
                }
                setLive(lastLiveState || currentState());
            });
        }

        async function refresh(logIt) {
            const state = await api("/api/state");
            setLive(state);
            if (logIt) {
                appendLog(el("actionLog"), {
                    event: "state",
                    mode: state.gameMode,
                    target: state.targetCode,
                    entry: state.entryWindow,
                    ids: [state.id0, state.id1, state.id2, state.id3],
                    storage: state.storagePresent,
                    doorOpen: state.doorOpen,
                    maglock: state.maglock
                });
            }
        }
        if (el("refreshBtn")) {
            el("refreshBtn").addEventListener("click", () => refresh(true).catch((e) => appendLog(el("actionLog"), String(e))));
        }
        document.querySelectorAll("[data-cmd]").forEach((btn) => {
            btn.addEventListener("click", async () => {
                try {
                    appendLog(el("actionLog"), await api("/api/command", { method: "POST", body: btn.getAttribute("data-cmd") }));
                    await refresh(true);
                } catch (e) {
                    appendLog(el("actionLog"), String(e));
                }
            });
        });
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh(true).catch((e) => appendLog(el("actionLog"), String(e)));
        setInterval(() => refresh(false).catch(() => {}), 2000);
    }

    async function pageMonitor() {
        async function refresh() {
            const state = await api("/api/monitor");
            if (el("monitorScenario")) {
                el("monitorScenario").textContent = (state.scenario || "live") + " · " + (state.gameMode === "code_entry" ? "code" : "io");
            }
            renderReedGrid(state);
            renderMonitorFacts(state);
            renderKeypadMatrix(state);
            renderMcp(state);
            const dump = el("monitorLog");
            if (dump && demoConfig.debug) {
                dump.classList.remove("hidden");
                dump.textContent = JSON.stringify({ reeds: state.reeds, keypadDown: state.keypadDown, entry: state.entryWindow }, null, 2);
            }
        }
        if (el("refreshMonitor")) {
            el("refreshMonitor").addEventListener("click", () => refresh().catch(() => {}));
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        refresh();
        setInterval(() => refresh().catch(() => {}), 400);
    }

    async function pageConfig() {
        const form = el("configForm");
        const log = el("configLog");
        try {
            const cfg = await api("/api/config");
            fillForm(form, cfg);
            fillCodeSets(cfg.codes);
            fillChargeWeights(cfg.chargeWeights);
            fillWeightSets(cfg.targetWeights);
            appendLog(log, cfg);
        } catch (e) {
            fillForm(form, demoConfig);
            fillCodeSets(demoConfig.codes);
            fillChargeWeights(demoConfig.chargeWeights);
            fillWeightSets(demoConfig.targetWeights);
            appendLog(log, String(e));
        }

        async function postConfig(path) {
            const body = {};
            collectForm(form, body);
            body.codes = collectCodeSets();
            body.chargeWeights = collectChargeWeights();
            demoConfig.chargeWeights = body.chargeWeights;
            body.targetWeights = collectWeightSets();
            body.weightSetting = demoConfig.weightSetting;
            document.querySelectorAll(".weight-set-row").forEach(refreshWeightSetRow);
            const bad = body.targetWeights.filter((w) => w.grams > 0 && countSolutions(w.grams) === 0);
            if (bad.length) {
                appendLog(log, {
                    error: "Target weight has no 4-charge solution",
                    labels: bad.map((w) => w.label + "=" + w.grams + " g")
                });
                return;
            }
            appendLog(log, await api(path, { method: "POST", body: JSON.stringify(body) }));
            saveDemoConfig(body);
        }

        if (el("applyConfig")) {
            el("applyConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("saveConfig")) {
            el("saveConfig").addEventListener("click", (ev) => {
                ev.preventDefault();
                postConfig("/api/config/save").catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("restoreDefaults")) {
            el("restoreDefaults").addEventListener("click", (ev) => {
                ev.preventDefault();
                api("/api/config/defaults").then((cfg) => {
                    fillForm(form, cfg);
                    fillCodeSets(cfg.codes);
                    fillChargeWeights(cfg.chargeWeights);
                    fillWeightSets(cfg.targetWeights);
                    saveDemoConfig(cfg);
                    appendLog(log, cfg);
                }).catch((e) => appendLog(log, String(e)));
            });
        }
        if (el("sendRaw")) {
            el("sendRaw").addEventListener("click", async () => {
                try {
                    appendLog(el("rawLog"), await api("/api/command", { method: "POST", body: el("rawCommand").value }));
                } catch (e) {
                    appendLog(el("rawLog"), String(e));
                }
            });
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
    }

    async function pageConnection() {
        const log = el("connectionLog");
        function fillTopics(conn) {
            ["mqttCommandTopic", "mqttStateTopic", "mqttEventsTopic", "mqttWarningsTopic"].forEach((id) => {
                if (el(id)) {
                    el(id).textContent = conn[id] || "";
                }
            });
        }
        try {
            const conn = await api("/api/connection");
            ["wifiSsid", "wifiPassword", "mqttHost", "mqttPort", "mqttUsername", "mqttPassword", "mqttBaseTopic", "mqttGameStateTopic", "mqttPropAnnounceTopic", "networkName", "apPassword"].forEach((id) => {
                if (el(id) && conn[id] != null) {
                    el(id).value = conn[id];
                }
            });
            if (el("apSsidDisplay") && conn.apSsid) {
                el("apSsidDisplay").value = conn.apSsid;
            }
            if (el("apEnabled")) {
                el("apEnabled").checked = Boolean(conn.apEnabled);
            }
            if (el("apIpNote") && conn.apIpAddress) {
                el("apIpNote").textContent = "AP IP Address: " + conn.apIpAddress;
            }
            fillTopics(conn);
            if (el("wifiStatus")) {
                el("wifiStatus").innerHTML = `<span class="wifi-icon">${tablerWifiSvg(4)}</span> Connected to <strong>${conn.wifiSsid}</strong>`;
            }
            appendLog(log, conn);
        } catch (e) {
            appendLog(log, String(e));
        }

        try {
            const scan = await api("/api/connection/scan");
            const list = el("ssidList");
            if (list && scan.networks) {
                list.innerHTML = "";
                scan.networks.forEach((n) => {
                    const b = document.createElement("button");
                    b.type = "button";
                    b.className = "ssid-item";
                    b.innerHTML = `<span>${n.ssid}</span><span class="ssid-meta"><span class="wifi-icon">${tablerWifiSvg(rssiLevel(n.rssi))}</span>${n.rssi} dBm</span>`;
                    b.addEventListener("click", () => {
                        if (el("wifiSsid")) {
                            el("wifiSsid").value = n.ssid;
                        }
                    });
                    list.appendChild(b);
                });
            }
        } catch {
            /* ignore */
        }

        async function loadDetails() {
            try {
                const d = await api("/api/details");
                const map = {
                    detailPropName: d.propName,
                    detailIpAddress: d.ipAddress,
                    detailSoftwareVersion: d.softwareVersion,
                    detailBuildNumber: d.buildNumber,
                    detailBuildDate: d.buildDate,
                    detailCpuTemp: d.cpuTemp,
                    detailFreeMemory: d.freeMemory
                };
                Object.keys(map).forEach((id) => {
                    if (el(id)) {
                        el(id).textContent = map[id] || "-";
                    }
                });
            } catch (e) {
                appendLog(el("deviceLog"), String(e));
            }
        }
        if (el("refreshDetails")) {
            el("refreshDetails").addEventListener("click", loadDetails);
        }
        if (el("connectWifi")) {
            el("connectWifi").addEventListener("click", async () => {
                appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            });
        }
        if (el("saveConnection")) {
            el("saveConnection").addEventListener("click", async () => {
                appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            });
        }
        if (el("applyTopics") || el("applyDeviceName")) {
            const apply = async () => appendLog(log, await api("/api/connection", { method: "POST", body: "{}" }));
            if (el("applyTopics")) {
                el("applyTopics").addEventListener("click", apply);
            }
            if (el("applyDeviceName")) {
                el("applyDeviceName").addEventListener("click", apply);
            }
        }
        fetchStatusIcons();
        setInterval(fetchStatusIcons, 10000);
        loadDetails();
    }

    window.PX = {
        pageDashboard: pageDashboard,
        pageConfig: pageConfig,
        pageMonitor: pageMonitor,
        pageConnection: pageConnection
    };
})();
