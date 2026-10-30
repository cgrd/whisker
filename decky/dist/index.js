const manifest = {"name":"Whisker"};
const API_VERSION = 2;
const internalAPIConnection = window.__DECKY_SECRET_INTERNALS_DO_NOT_USE_OR_YOU_WILL_BE_FIRED_deckyLoaderAPIInit;
if (!internalAPIConnection) {
    throw new Error('[@decky/api]: Failed to connect to the loader as as the loader API was not initialized. This is likely a bug in Decky Loader.');
}
let api;
try {
    api = internalAPIConnection.connect(API_VERSION, manifest.name);
}
catch {
    api = internalAPIConnection.connect(1, manifest.name);
    console.warn(`[@decky/api] Requested API version ${API_VERSION} but the running loader only supports version 1. Some features may not work.`);
}
if (api._version != API_VERSION) {
    console.warn(`[@decky/api] Requested API version ${API_VERSION} but the running loader only supports version ${api._version}. Some features may not work.`);
}
const callable = api.callable;

var DefaultContext = {
  color: undefined,
  size: undefined,
  className: undefined,
  style: undefined,
  attr: undefined
};
var IconContext = SP_REACT.createContext && /*#__PURE__*/SP_REACT.createContext(DefaultContext);

var _excluded = ["attr", "size", "title"];
function _objectWithoutProperties(e, t) { if (null == e) return {}; var o, r, i = _objectWithoutPropertiesLoose(e, t); if (Object.getOwnPropertySymbols) { var n = Object.getOwnPropertySymbols(e); for (r = 0; r < n.length; r++) o = n[r], -1 === t.indexOf(o) && {}.propertyIsEnumerable.call(e, o) && (i[o] = e[o]); } return i; }
function _objectWithoutPropertiesLoose(r, e) { if (null == r) return {}; var t = {}; for (var n in r) if ({}.hasOwnProperty.call(r, n)) { if (-1 !== e.indexOf(n)) continue; t[n] = r[n]; } return t; }
function _extends() { return _extends = Object.assign ? Object.assign.bind() : function (n) { for (var e = 1; e < arguments.length; e++) { var t = arguments[e]; for (var r in t) ({}).hasOwnProperty.call(t, r) && (n[r] = t[r]); } return n; }, _extends.apply(null, arguments); }
function ownKeys(e, r) { var t = Object.keys(e); if (Object.getOwnPropertySymbols) { var o = Object.getOwnPropertySymbols(e); r && (o = o.filter(function (r) { return Object.getOwnPropertyDescriptor(e, r).enumerable; })), t.push.apply(t, o); } return t; }
function _objectSpread(e) { for (var r = 1; r < arguments.length; r++) { var t = null != arguments[r] ? arguments[r] : {}; r % 2 ? ownKeys(Object(t), true).forEach(function (r) { _defineProperty(e, r, t[r]); }) : Object.getOwnPropertyDescriptors ? Object.defineProperties(e, Object.getOwnPropertyDescriptors(t)) : ownKeys(Object(t)).forEach(function (r) { Object.defineProperty(e, r, Object.getOwnPropertyDescriptor(t, r)); }); } return e; }
function _defineProperty(e, r, t) { return (r = _toPropertyKey(r)) in e ? Object.defineProperty(e, r, { value: t, enumerable: true, configurable: true, writable: true }) : e[r] = t, e; }
function _toPropertyKey(t) { var i = _toPrimitive(t, "string"); return "symbol" == typeof i ? i : i + ""; }
function _toPrimitive(t, r) { if ("object" != typeof t || !t) return t; var e = t[Symbol.toPrimitive]; if (void 0 !== e) { var i = e.call(t, r); if ("object" != typeof i) return i; throw new TypeError("@@toPrimitive must return a primitive value."); } return ("string" === r ? String : Number)(t); }
function Tree2Element(tree) {
  return tree && tree.map((node, i) => /*#__PURE__*/SP_REACT.createElement(node.tag, _objectSpread({
    key: i
  }, node.attr), Tree2Element(node.child)));
}
function GenIcon(data) {
  return props => /*#__PURE__*/SP_REACT.createElement(IconBase, _extends({
    attr: _objectSpread({}, data.attr)
  }, props), Tree2Element(data.child));
}
function IconBase(props) {
  var elem = conf => {
    var attr = props.attr,
      size = props.size,
      title = props.title,
      svgProps = _objectWithoutProperties(props, _excluded);
    var computedSize = size || conf.size || "1em";
    var className;
    if (conf.className) className = conf.className;
    if (props.className) className = (className ? className + " " : "") + props.className;
    return /*#__PURE__*/SP_REACT.createElement("svg", _extends({
      stroke: "currentColor",
      fill: "currentColor",
      strokeWidth: "0"
    }, conf.attr, attr, svgProps, {
      className: className,
      style: _objectSpread(_objectSpread({
        color: props.color || conf.color
      }, conf.style), props.style),
      height: computedSize,
      width: computedSize,
      xmlns: "http://www.w3.org/2000/svg"
    }), title && /*#__PURE__*/SP_REACT.createElement("title", null, title), props.children);
  };
  return IconContext !== undefined ? /*#__PURE__*/SP_REACT.createElement(IconContext.Consumer, null, conf => elem(conf)) : elem(DefaultContext);
}

// THIS FILE IS AUTO GENERATED
function FaMicrophone (props) {
  return GenIcon({"attr":{"viewBox":"0 0 352 512"},"child":[{"tag":"path","attr":{"d":"M176 352c53.02 0 96-42.98 96-96V96c0-53.02-42.98-96-96-96S80 42.98 80 96v160c0 53.02 42.98 96 96 96zm160-160h-16c-8.84 0-16 7.16-16 16v48c0 74.8-64.49 134.82-140.79 127.38C96.71 376.89 48 317.11 48 250.3V208c0-8.84-7.16-16-16-16H16c-8.84 0-16 7.16-16 16v40.16c0 89.64 63.97 169.55 152 181.69V464H96c-8.84 0-16 7.16-16 16v16c0 8.84 7.16 16 16 16h160c8.84 0 16-7.16 16-16v-16c0-8.84-7.16-16-16-16h-56v-33.77C285.71 418.47 352 344.9 352 256v-48c0-8.84-7.16-16-16-16z"},"child":[]}]})(props);
}

const getStatus = callable("get_status");
const startService = callable("start_service");
const stopService = callable("stop_service");
const enableService = callable("enable_service");
const disableService = callable("disable_service");
const getConfig = callable("get_config");
const setOption = callable("set_option");
const getAudioSources = callable("get_audio_sources");
const getModels = callable("get_models");
const startRecording = callable("start_recording");
const stopRecording = callable("stop_recording");
const restartService = callable("restart_service");
const getLogs = callable("get_logs");
const getDiagnostics = callable("get_diagnostics");
const getLastTranscription = callable("get_last_transcription");
const getKnownModels = callable("get_known_models");
const getModelStatus = callable("get_model_status");
const downloadModel = callable("download_model");
const cancelDownload = callable("cancel_download");
const captureButton = callable("capture_button");
const DEFAULTS = {
    model: "",
    backend: "vulkan",
    audio_device: "",
    trigger_key: "",
    trigger_button: "",
    trigger_mode: "hold",
    overlay_position: "bottom",
    submit_key: "none",
    history: 10,
    noise_threshold: 0.005,
    overlay: true,
    tray: true,
    audio_cue: true,
    haptic: true,
    dictation_commands: true,
    auto_capitalize: true,
    undo_enabled: true,
    recording_timeout: 30,
};
const PEDAL_DEFAULTS = {
    action: "none",
    key: "",
    mode: "tap",
    output: "type",
    suffix: "none",
};
const PEDALS = [["left", "Left Pedal"], ["center", "Center Pedal"], ["right", "Right Pedal"]];
const SLIDER_DELAY_MS = 500;
const TEXT_DELAY_MS = 1500;
const RESTART_DELAY_MS = 1000;
const options = (pairs) => pairs.map(([data, label]) => ({ data, label }));
const modeOptions = options([["hold", "Hold"], ["toggle", "Toggle"]]);
const submitKeyOptions = options([["none", "None"], ["enter", "Enter"], ["tab", "Tab"]]);
const overlayPositionOptions = options([
    ["bottom", "Bottom"], ["top", "Top"], ["bottom-left", "Bottom Left"],
    ["bottom-right", "Bottom Right"], ["top-left", "Top Left"], ["top-right", "Top Right"],
]);
const buttonOptions = options([
    ["", "None"], ["L4", "L4 (Left Back Upper)"], ["R4", "R4 (Right Back Upper)"],
    ["L5", "L5 (Left Back Lower)"], ["R5", "R5 (Right Back Lower)"], ["select", "Select / Back"],
    ["start", "Start / Menu"], ["guide", "Guide / Steam"], ["assistant", "Assistant (Stadia)"],
    ["capture", "Capture (Stadia)"], ["L1", "L1"], ["R1", "R1"],
    ["L3", "L3 (Stick Click)"], ["R3", "R3 (Stick Click)"], ["A", "A"], ["B", "B"], ["X", "X"], ["Y", "Y"],
    ["L1+R1", "L1 + R1"], ["L3+R3", "L3 + R3 (Both Stick Clicks)"],
]);
const backendOptions = options([["vulkan", "Vulkan (GPU)"], ["cpu", "CPU"]]);
const actionOptions = options([["none", "None"], ["key", "Key"], ["transcribe", "Transcribe"]]);
// Commands can only be set in config.yaml; the plugin just shows that one is configured
const execActionOption = { data: "exec", label: "Run Command (config file)" };
const pedalModeOptions = options([["tap", "Tap"], ["hold", "Hold"]]);
const outputOptions = options([["type", "Type"], ["clipboard", "Clipboard"], ["both", "Both"]]);
function readPath(obj, path) {
    return path.split(".").reduce((node, key) => (node && typeof node === "object" ? node[key] : undefined), obj);
}
function writePath(obj, [key, ...rest], value) {
    const node = obj && typeof obj === "object" ? obj : {};
    return { ...node, [key]: rest.length ? writePath(node[key], rest, value) : value };
}
function defaultFor(path) {
    return path.startsWith("pedals.") ? PEDAL_DEFAULTS[path.split(".")[2]] : DEFAULTS[path];
}
const row = (child) => SP_JSX.jsx(DFL.PanelSectionRow, { children: child });
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function WhiskerPanel() {
    const [running, setRunning] = SP_REACT.useState(false);
    const [recording, setRecording] = SP_REACT.useState(false);
    const [logs, setLogs] = SP_REACT.useState("");
    const [showLogs, setShowLogs] = SP_REACT.useState(false);
    const [diagnostics, setDiagnostics] = SP_REACT.useState(null);
    const [lastTranscription, setLastTranscription] = SP_REACT.useState("");
    const [modelStatus, setModelStatus] = SP_REACT.useState(null);
    const [enabled, setEnabled] = SP_REACT.useState(false);
    const [models, setModels] = SP_REACT.useState([]);
    const [knownModels, setKnownModels] = SP_REACT.useState(null);
    const [audioSources, setAudioSources] = SP_REACT.useState([]);
    const [config, setConfig] = SP_REACT.useState({});
    const [captureText, setCaptureText] = SP_REACT.useState("");
    const mountedRef = SP_REACT.useRef(true);
    const saveTimers = SP_REACT.useRef({});
    const restartTimer = SP_REACT.useRef(undefined);
    const refreshStatus = SP_REACT.useCallback(async () => {
        const status = await getStatus();
        if (!mountedRef.current)
            return;
        setRunning(status.running);
        setEnabled(status.enabled);
    }, []);
    SP_REACT.useEffect(() => {
        mountedRef.current = true;
        refreshStatus();
        (async () => {
            const c = await getConfig();
            if (mountedRef.current)
                setConfig(c);
        })();
        (async () => {
            const s = await getAudioSources();
            if (mountedRef.current)
                setAudioSources(s);
        })();
        (async () => {
            const k = await getKnownModels();
            if (mountedRef.current)
                setKnownModels(k);
        })();
        (async () => {
            const m = await getModels();
            if (mountedRef.current)
                setModels(m);
            const ms = await getModelStatus();
            if (!mountedRef.current)
                return;
            setModelStatus(ms);
            if (ms.downloading)
                pollDownload();
        })();
        const interval = setInterval(refreshStatus, 5000);
        return () => {
            mountedRef.current = false;
            clearInterval(interval);
        };
    }, [refreshStatus]);
    function get(path) {
        const value = readPath(config, path);
        const fallback = defaultFor(path);
        return (typeof value === typeof fallback ? value : fallback);
    }
    const scheduleRestart = () => {
        clearTimeout(restartTimer.current);
        restartTimer.current = setTimeout(async () => {
            const status = await getStatus();
            if (status.running)
                await restartService();
        }, RESTART_DELAY_MS);
    };
    // Timers deliberately survive unmount so closing the panel mid-debounce still saves.
    const update = (path, value, delay = 0) => {
        setConfig((c) => writePath(c, path.split("."), value));
        clearTimeout(saveTimers.current[path]);
        saveTimers.current[path] = setTimeout(async () => {
            await setOption(path, value);
            scheduleRestart();
        }, delay);
    };
    const toggle = (path, label, description) => row(SP_JSX.jsx(DFL.ToggleField, { label: label, description: description, checked: get(path), onChange: (v) => update(path, v) }));
    const dropdown = (path, label, rgOptions, description) => row(SP_JSX.jsx(DFL.DropdownItem, { label: label, description: description, rgOptions: rgOptions, selectedOption: get(path), onChange: (o) => update(path, o.data) }));
    const slider = (path, label, min, max, step, description) => row(SP_JSX.jsx(DFL.SliderField, { label: label, description: description, value: get(path), min: min, max: max, step: step, showValue: true, onChange: (v) => update(path, v, SLIDER_DELAY_MS) }));
    const text = (path, label, description) => row(SP_JSX.jsx(DFL.TextField, { label: label, description: description, value: get(path), onChange: (e) => update(path, e.target.value, TEXT_DELAY_MS) }));
    const handleCapture = async () => {
        setCaptureText("Press and release a button or chord on any controller...");
        const result = await captureButton();
        if (result.button)
            update("trigger_button", result.button);
        if (!mountedRef.current)
            return;
        setCaptureText(result.button ? `Using ${result.button} on ${result.device}` : result.error ?? "");
    };
    const triggerButton = get("trigger_button");
    const triggerOptions = buttonOptions.some((o) => o.data === triggerButton)
        ? buttonOptions : [...buttonOptions, { data: triggerButton, label: triggerButton }];
    const pedalSection = ([name, title]) => {
        const p = (key) => `pedals.${name}.${key}`;
        const action = get(p("action"));
        const actions = action === "exec" ? [...actionOptions, execActionOption] : actionOptions;
        return (SP_JSX.jsxs(DFL.PanelSection, { title: title, children: [dropdown(p("action"), "Action", actions), action === "key" && text(p("key"), "Key Combo", "e.g. ctrl+c"), action === "key" && dropdown(p("mode"), "Mode", pedalModeOptions), action === "transcribe" && dropdown(p("output"), "Output", outputOptions), action === "transcribe" &&
                    dropdown(p("suffix"), "Suffix Key", submitKeyOptions, "Key pressed after the text"), action === "exec" && row(SP_JSX.jsx("div", { style: { fontSize: "12px", padding: "8px" }, children: "Runs the command set in ~/.config/pedal-stt/config.yaml" }))] }, name));
    };
    const toggleRunning = async (value) => {
        if (value) {
            await startService();
        }
        else {
            await stopService();
        }
        await refreshStatus();
    };
    const toggleEnabled = async (value) => {
        if (value) {
            await enableService();
        }
        else {
            await disableService();
        }
        await refreshStatus();
    };
    const toggleRecording = async () => {
        if (recording) {
            await stopRecording();
            setRecording(false);
            await sleep(3000);
            const transcription = await getLastTranscription();
            if (transcription)
                setLastTranscription(transcription);
        }
        else {
            const ok = await startRecording();
            if (ok)
                setRecording(true);
        }
    };
    const handleRestart = async () => {
        setRecording(false);
        await restartService();
        await sleep(2000);
        await refreshStatus();
    };
    const handleShowLogs = async () => {
        if (showLogs) {
            setShowLogs(false);
        }
        else {
            setLogs(await getLogs(30));
            setShowLogs(true);
        }
    };
    const handleRefreshLogs = async () => {
        setLogs(await getLogs(30));
    };
    const pollDownload = () => {
        const pollId = setInterval(async () => {
            const ms = await getModelStatus();
            if (!mountedRef.current) {
                clearInterval(pollId);
                return;
            }
            setModelStatus(ms);
            if (ms.downloading)
                return;
            clearInterval(pollId);
            const m = await getModels();
            if (!mountedRef.current)
                return;
            setModels(m);
            setConfig((c) => ({ ...c, model: ms.configured }));
        }, 2000);
    };
    // Downloads only the model the user clicked; there is no default
    const handlePickModel = async (file) => {
        await downloadModel(file);
        const ms = await getModelStatus();
        if (!mountedRef.current)
            return;
        setModelStatus(ms);
        if (ms.downloading)
            pollDownload();
        else
            setConfig((c) => ({ ...c, model: ms.configured }));
    };
    const handleCancelDownload = async () => {
        await cancelDownload();
        const ms = await getModelStatus();
        if (mountedRef.current)
            setModelStatus(ms);
    };
    const handleDiagnostics = async () => {
        setDiagnostics(await getDiagnostics());
    };
    const knownModel = (path) => knownModels?.models.find((k) => path.split("/").pop() === k.file);
    const modelOptions = models.map((m) => ({ data: m, label: knownModel(m)?.label ?? m.split("/").pop() ?? m }));
    const selectedModel = knownModel(get("model"));
    const hasModel = modelStatus?.configured_exists ?? true;
    const downloadingModel = knownModels?.models.find((k) => k.file === modelStatus?.downloading);
    const modelChoice = (m) => {
        const downloaded = modelStatus?.downloaded.includes(m.file) ?? false;
        if (hasModel && downloaded)
            return null;
        const description = m.recommended ? `${m.note} ${knownModels?.recommended_reason ?? ""}` : m.note;
        return SP_JSX.jsx(DFL.PanelSectionRow, { children: SP_JSX.jsx(DFL.ButtonItem, { label: m.recommended ? `${m.label} (recommended)` : m.label, description: description, onClick: () => handlePickModel(m.file), disabled: !!downloadingModel, layout: "below", children: downloaded ? "Use This Model" : "Download" }) }, m.file);
    };
    const link = (label, url) => row(SP_JSX.jsx(DFL.ButtonItem, { label: label, description: url.replace("https://", ""), onClick: () => DFL.Navigation.NavigateToExternalWeb(url), layout: "below" }));
    const audioOptions = [{ data: "", label: "Default" }, ...audioSources.map((s) => ({ data: s.id, label: s.name }))];
    const diagnosticsText = diagnostics
        ? `Service: ${diagnostics.running ? "running" : "stopped"} | Socket: ${diagnostics.socket ? "ok" : "missing"}`
            + ` | Model: ${diagnostics.model_exists ? "ok" : "missing"} | RAM: ${diagnostics.memory_mb}MB`
        : "Check service health";
    return (SP_JSX.jsxs(SP_JSX.Fragment, { children: [SP_JSX.jsxs(DFL.PanelSection, { title: "Service", children: [row(SP_JSX.jsx(DFL.ToggleField, { label: "Whisker", description: running ? "Running" : "Stopped", checked: running, onChange: toggleRunning })), row(SP_JSX.jsx(DFL.ToggleField, { label: "Auto-Start", description: "Start on login", checked: enabled, onChange: toggleEnabled })), running && row(SP_JSX.jsx(DFL.ButtonItem, { label: recording ? "Stop Recording" : "Record", description: recording ? "Recording in progress..." : "Tap to start dictation", onClick: toggleRecording, layout: "below" })), lastTranscription && row(SP_JSX.jsx("div", { style: { fontSize: "12px", padding: "8px", background: "rgba(0,0,0,0.2)",
                            borderRadius: "4px", fontStyle: "italic" }, children: lastTranscription }))] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Model", children: [!hasModel && row(SP_JSX.jsx("div", { style: { padding: "8px", fontSize: "12px" }, children: "Pick a speech model to start dictating. Nothing downloads until you choose one." })), hasModel && modelOptions.length > 0 &&
                        dropdown("model", "Transcription Model", modelOptions, selectedModel?.note), downloadingModel ? (SP_JSX.jsxs(SP_JSX.Fragment, { children: [row(SP_JSX.jsx("div", { style: { padding: "8px", fontSize: "12px" }, children: `Downloading ${downloadingModel.label}: ${modelStatus?.progress_mb ?? 0}`
                                    + ` / ${modelStatus?.total_mb ?? 0} MB` })), row(SP_JSX.jsx(DFL.ButtonItem, { label: "Cancel Download", onClick: handleCancelDownload, layout: "below" }))] })) : knownModels?.models.map(modelChoice), modelStatus?.error && row(SP_JSX.jsx("div", { style: { padding: "8px", fontSize: "12px", color: "#ff8080" }, children: modelStatus.error })), knownModels && link("Model Files", knownModels.repo), knownModels && link("Original NVIDIA Model", knownModels.original)] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Trigger", children: [dropdown("trigger_button", "Controller Button", triggerOptions, "Hold to talk. Unmap this button in Steam Input so no game uses it"), row(SP_JSX.jsx(DFL.ButtonItem, { label: "Press the Button You Want", description: captureText || "Works with any connected controller; hold two buttons for a chord", onClick: handleCapture, layout: "below" })), dropdown("trigger_mode", "Mode", modeOptions, "Hold: release to stop. Toggle: press again to stop.")] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Audio", children: [dropdown("audio_device", "Input Device", audioOptions), slider("noise_threshold", "Noise Gate", 0, 0.01, 0.0001, "Ignore audio quieter than this")] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Overlay", children: [toggle("overlay", "Show Overlay", "Show overlay during recording"), dropdown("overlay_position", "Position", overlayPositionOptions, "Where the overlay appears on screen")] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Feedback", children: [toggle("audio_cue", "Audio Cue", "Beep on record start/stop"), toggle("haptic", "Haptic Feedback", "Controller vibration on record start/stop")] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Text Processing", children: [toggle("dictation_commands", "Dictation Commands", "Replace 'period', 'comma', etc. with punctuation"), toggle("auto_capitalize", "Auto-Capitalize", "Capitalize after sentences and newlines"), toggle("undo_enabled", "Undo", "Quick tap to delete last transcription"), dropdown("submit_key", "Send After Dictation", submitKeyOptions, "Key pressed after the text is typed"), slider("recording_timeout", "Recording Timeout", 0, 120, 5, "Auto-stop after N seconds (0 = no limit)")] }), PEDALS.map(pedalSection), SP_JSX.jsxs(DFL.PanelSection, { title: "Advanced", children: [text("trigger_key", "Keyboard Shortcut", "Hold to record, e.g. ctrl+shift+space. Empty to disable"), dropdown("backend", "Backend", backendOptions, "Takes effect on restart"), text("model", "Model Path", "Path to a GGUF model file"), slider("history", "History Entries", 0, 100, 5, "Transcriptions to keep"), toggle("tray", "Tray Icon", "Show system tray icon (desktop mode)")] }), SP_JSX.jsxs(DFL.PanelSection, { title: "Troubleshooting", children: [row(SP_JSX.jsx(DFL.ButtonItem, { label: "Restart Service", description: "Stop and restart Whisker", onClick: handleRestart, layout: "below" })), row(SP_JSX.jsx(DFL.ButtonItem, { label: "Run Diagnostics", description: diagnosticsText, onClick: handleDiagnostics, layout: "below" })), row(SP_JSX.jsx(DFL.ButtonItem, { label: showLogs ? "Hide Logs" : "View Logs", description: "Show recent service logs", onClick: handleShowLogs, layout: "below" })), showLogs && row(SP_JSX.jsxs("div", { children: [SP_JSX.jsx("pre", { style: { fontSize: "10px", maxHeight: "200px", overflow: "auto", whiteSpace: "pre-wrap",
                                    wordBreak: "break-all", background: "rgba(0,0,0,0.3)", padding: "8px",
                                    borderRadius: "4px" }, children: logs }), SP_JSX.jsx(DFL.ButtonItem, { label: "Refresh", onClick: handleRefreshLogs, layout: "below" })] }))] })] }));
}
var index = DFL.definePlugin(() => ({
    name: "Whisker",
    titleView: SP_JSX.jsx("div", { className: DFL.staticClasses.Title, children: "Whisker" }),
    content: SP_JSX.jsx(WhiskerPanel, {}),
    icon: SP_JSX.jsx(FaMicrophone, {}),
}));

export { index as default };
//# sourceMappingURL=index.js.map
