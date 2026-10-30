// SPDX-License-Identifier: MIT
import { callable } from "@decky/api";
import {
    ButtonItem,
    definePlugin,
    DropdownItem,
    Navigation,
    PanelSection,
    PanelSectionRow,
    SliderField,
    staticClasses,
    TextField,
    ToggleField,
} from "@decky/ui";
import { ReactNode, useCallback, useEffect, useRef, useState } from "react";
import { FaMicrophone } from "react-icons/fa";

type Config = Record<string, unknown>;
type Status = { running: boolean; enabled: boolean };
type AudioSource = { id: string; name: string };
type ModelStatus = {
    configured: string;
    configured_exists: boolean;
    downloaded: string[];
    downloading: string;
    progress_mb: number;
    total_mb: number;
    error: string;
};
type Diagnostics = {
    running: boolean;
    socket: boolean;
    model_exists: boolean;
    model_path: string;
    memory_mb: number;
};
type KnownModel = { file: string; label: string; note: string; recommended: boolean };
type KnownModels = { repo: string; original: string; recommended_reason: string; models: KnownModel[] };
type Option = { data: string; label: string };
type CaptureResult = { button?: string; device?: string; error?: string };
type Value = string | number | boolean;

const getStatus = callable<[], Status>("get_status");
const startService = callable<[], void>("start_service");
const stopService = callable<[], void>("stop_service");
const enableService = callable<[], void>("enable_service");
const disableService = callable<[], void>("disable_service");
const getConfig = callable<[], Config>("get_config");
const setOption = callable<[path: string, value: Value], void>("set_option");
const getAudioSources = callable<[], AudioSource[]>("get_audio_sources");
const getModels = callable<[], string[]>("get_models");
const startRecording = callable<[], boolean>("start_recording");
const stopRecording = callable<[], boolean>("stop_recording");
const restartService = callable<[], void>("restart_service");
const getLogs = callable<[lines: number], string>("get_logs");
const getDiagnostics = callable<[], Diagnostics>("get_diagnostics");
const getLastTranscription = callable<[], string>("get_last_transcription");
const getKnownModels = callable<[], KnownModels>("get_known_models");
const getModelStatus = callable<[], ModelStatus>("get_model_status");
const downloadModel = callable<[file: string], boolean>("download_model");
const cancelDownload = callable<[], void>("cancel_download");
const captureButton = callable<[], CaptureResult>("capture_button");
const getChatProfiles = callable<[], string[]>("get_chat_profiles");

const DEFAULTS: Record<string, Value> = {
    model: "",
    backend: "vulkan",
    audio_device: "",
    trigger_key: "",
    trigger_button: "",
    trigger_mode: "hold",
    overlay_position: "bottom",
    submit_key: "none",
    chat_profile: "",
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
const PEDAL_DEFAULTS: Record<string, Value> = {
    action: "none",
    key: "",
    mode: "tap",
    output: "type",
    suffix: "none",
};
const PEDALS: [string, string][] = [["left", "Left Pedal"], ["center", "Center Pedal"], ["right", "Right Pedal"]];
const SLIDER_DELAY_MS = 500;
const TEXT_DELAY_MS = 1500;
const RESTART_DELAY_MS = 1000;

const options = (pairs: [string, string][]): Option[] => pairs.map(([data, label]) => ({ data, label }));
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
const CHAT_PROFILE_LABELS: Record<string, string> = { wow: "World of Warcraft" };
const actionOptions = options([["none", "None"], ["key", "Key"], ["transcribe", "Transcribe"]]);
// Commands can only be set in config.yaml; the plugin just shows that one is configured
const execActionOption: Option = { data: "exec", label: "Run Command (config file)" };
const pedalModeOptions = options([["tap", "Tap"], ["hold", "Hold"]]);
const outputOptions = options([["type", "Type"], ["clipboard", "Clipboard"], ["both", "Both"]]);

function readPath(obj: unknown, path: string): unknown {
    return path.split(".").reduce<unknown>(
        (node, key) => (node && typeof node === "object" ? (node as Config)[key] : undefined), obj);
}

function writePath(obj: unknown, [key, ...rest]: string[], value: Value): Config {
    const node = obj && typeof obj === "object" ? (obj as Config) : {};
    return { ...node, [key]: rest.length ? writePath(node[key], rest, value) : value };
}

function defaultFor(path: string): Value {
    return path.startsWith("pedals.") ? PEDAL_DEFAULTS[path.split(".")[2]] : DEFAULTS[path];
}

const row = (child: ReactNode) => <PanelSectionRow>{child}</PanelSectionRow>;
const sleep = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms));

function WhiskerPanel() {
    const [running, setRunning] = useState(false);
    const [recording, setRecording] = useState(false);
    const [logs, setLogs] = useState("");
    const [showLogs, setShowLogs] = useState(false);
    const [diagnostics, setDiagnostics] = useState<Diagnostics | null>(null);
    const [lastTranscription, setLastTranscription] = useState("");
    const [modelStatus, setModelStatus] = useState<ModelStatus | null>(null);
    const [enabled, setEnabled] = useState(false);
    const [models, setModels] = useState<string[]>([]);
    const [knownModels, setKnownModels] = useState<KnownModels | null>(null);
    const [audioSources, setAudioSources] = useState<AudioSource[]>([]);
    const [chatProfiles, setChatProfiles] = useState<string[]>([]);
    const [config, setConfig] = useState<Config>({});
    const [captureText, setCaptureText] = useState("");
    const mountedRef = useRef(true);
    const saveTimers = useRef<Record<string, ReturnType<typeof setTimeout>>>({});
    const restartTimer = useRef<ReturnType<typeof setTimeout> | undefined>(undefined);

    const refreshStatus = useCallback(async () => {
        const status = await getStatus();
        if (!mountedRef.current) return;
        setRunning(status.running);
        setEnabled(status.enabled);
    }, []);

    useEffect(() => {
        mountedRef.current = true;
        refreshStatus();
        (async () => {
            const c = await getConfig();
            if (mountedRef.current) setConfig(c);
        })();
        (async () => {
            const s = await getAudioSources();
            if (mountedRef.current) setAudioSources(s);
        })();
        (async () => {
            const p = await getChatProfiles();
            if (mountedRef.current) setChatProfiles(p);
        })();
        (async () => {
            const k = await getKnownModels();
            if (mountedRef.current) setKnownModels(k);
        })();
        (async () => {
            const m = await getModels();
            if (mountedRef.current) setModels(m);
            const ms = await getModelStatus();
            if (!mountedRef.current) return;
            setModelStatus(ms);
            if (ms.downloading) pollDownload();
        })();
        const interval = setInterval(refreshStatus, 5000);
        return () => {
            mountedRef.current = false;
            clearInterval(interval);
        };
    }, [refreshStatus]);

    function get<T extends Value>(path: string): T {
        const value = readPath(config, path);
        const fallback = defaultFor(path);
        return (typeof value === typeof fallback ? value : fallback) as T;
    }

    const scheduleRestart = () => {
        clearTimeout(restartTimer.current);
        restartTimer.current = setTimeout(async () => {
            const status = await getStatus();
            if (status.running) await restartService();
        }, RESTART_DELAY_MS);
    };

    // Timers deliberately survive unmount so closing the panel mid-debounce still saves.
    const update = (path: string, value: Value, delay = 0) => {
        setConfig((c) => writePath(c, path.split("."), value));
        clearTimeout(saveTimers.current[path]);
        saveTimers.current[path] = setTimeout(async () => {
            await setOption(path, value);
            scheduleRestart();
        }, delay);
    };

    const toggle = (path: string, label: string, description?: string) => row(
        <ToggleField label={label} description={description} checked={get<boolean>(path)}
            onChange={(v) => update(path, v)} />);
    const dropdown = (path: string, label: string, rgOptions: Option[], description?: string) => row(
        <DropdownItem label={label} description={description} rgOptions={rgOptions}
            selectedOption={get<string>(path)} onChange={(o) => update(path, o.data)} />);
    const slider = (path: string, label: string, min: number, max: number, step: number, description?: string) => row(
        <SliderField label={label} description={description} value={get<number>(path)} min={min} max={max}
            step={step} showValue={true} onChange={(v) => update(path, v, SLIDER_DELAY_MS)} />);
    const text = (path: string, label: string, description?: string) => row(
        <TextField label={label} description={description} value={get<string>(path)}
            onChange={(e) => update(path, e.target.value, TEXT_DELAY_MS)} />);

    const handleCapture = async () => {
        setCaptureText("Press and release a button or chord on any controller...");
        const result = await captureButton();
        if (result.button) update("trigger_button", result.button);
        if (!mountedRef.current) return;
        setCaptureText(result.button ? `Using ${result.button} on ${result.device}` : result.error ?? "");
    };

    const chatProfileOptions: Option[] = [{ data: "", label: "None" },
        ...chatProfiles.map((name) => ({ data: name, label: CHAT_PROFILE_LABELS[name] ?? name }))];

    const triggerButton = get<string>("trigger_button");
    const triggerOptions = buttonOptions.some((o) => o.data === triggerButton)
        ? buttonOptions : [...buttonOptions, { data: triggerButton, label: triggerButton }];

    const pedalSection = ([name, title]: [string, string]) => {
        const p = (key: string) => `pedals.${name}.${key}`;
        const action = get<string>(p("action"));
        const actions = action === "exec" ? [...actionOptions, execActionOption] : actionOptions;
        return (
            <PanelSection title={title} key={name}>
                {dropdown(p("action"), "Action", actions)}
                {action === "key" && text(p("key"), "Key Combo", "e.g. ctrl+c")}
                {action === "key" && dropdown(p("mode"), "Mode", pedalModeOptions)}
                {action === "transcribe" && dropdown(p("output"), "Output", outputOptions)}
                {action === "transcribe" &&
                    dropdown(p("suffix"), "Suffix Key", submitKeyOptions, "Key pressed after the text")}
                {action === "exec" && row(
                    <div style={{ fontSize: "12px", padding: "8px" }}>
                        Runs the command set in ~/.config/pedal-stt/config.yaml
                    </div>)}
            </PanelSection>
        );
    };

    const toggleRunning = async (value: boolean) => {
        if (value) {
            await startService();
        } else {
            await stopService();
        }
        await refreshStatus();
    };

    const toggleEnabled = async (value: boolean) => {
        if (value) {
            await enableService();
        } else {
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
            if (transcription) setLastTranscription(transcription);
        } else {
            const ok = await startRecording();
            if (ok) setRecording(true);
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
        } else {
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
            if (ms.downloading) return;
            clearInterval(pollId);
            const m = await getModels();
            if (!mountedRef.current) return;
            setModels(m);
            setConfig((c) => ({ ...c, model: ms.configured }));
        }, 2000);
    };

    // Downloads only the model the user clicked; there is no default
    const handlePickModel = async (file: string) => {
        await downloadModel(file);
        const ms = await getModelStatus();
        if (!mountedRef.current) return;
        setModelStatus(ms);
        if (ms.downloading) pollDownload();
        else setConfig((c) => ({ ...c, model: ms.configured }));
    };

    const handleCancelDownload = async () => {
        await cancelDownload();
        const ms = await getModelStatus();
        if (mountedRef.current) setModelStatus(ms);
    };

    const handleDiagnostics = async () => {
        setDiagnostics(await getDiagnostics());
    };

    const knownModel = (path: string) => knownModels?.models.find((k) => path.split("/").pop() === k.file);
    const modelOptions = models.map((m) => ({ data: m, label: knownModel(m)?.label ?? m.split("/").pop() ?? m }));
    const selectedModel = knownModel(get<string>("model"));
    const hasModel = modelStatus?.configured_exists ?? true;
    const downloadingModel = knownModels?.models.find((k) => k.file === modelStatus?.downloading);
    const modelChoice = (m: KnownModel) => {
        const downloaded = modelStatus?.downloaded.includes(m.file) ?? false;
        if (hasModel && downloaded) return null;
        const description = m.recommended ? `${m.note} ${knownModels?.recommended_reason ?? ""}` : m.note;
        return <PanelSectionRow key={m.file}>
            <ButtonItem label={m.recommended ? `${m.label} (recommended)` : m.label} description={description}
                onClick={() => handlePickModel(m.file)} disabled={!!downloadingModel} layout="below">
                {downloaded ? "Use This Model" : "Download"}
            </ButtonItem>
        </PanelSectionRow>;
    };
    const link = (label: string, url: string) => row(
        <ButtonItem label={label} description={url.replace("https://", "")}
            onClick={() => Navigation.NavigateToExternalWeb(url)} layout="below" />);
    const audioOptions = [{ data: "", label: "Default" }, ...audioSources.map((s) => ({ data: s.id, label: s.name }))];
    const diagnosticsText = diagnostics
        ? `Service: ${diagnostics.running ? "running" : "stopped"} | Socket: ${diagnostics.socket ? "ok" : "missing"}`
            + ` | Model: ${diagnostics.model_exists ? "ok" : "missing"} | RAM: ${diagnostics.memory_mb}MB`
        : "Check service health";

    return (
        <>
            <PanelSection title="Service">
                {row(<ToggleField label="Whisker" description={running ? "Running" : "Stopped"} checked={running}
                    onChange={toggleRunning} />)}
                {row(<ToggleField label="Auto-Start" description="Start on login" checked={enabled}
                    onChange={toggleEnabled} />)}
                {running && row(<ButtonItem label={recording ? "Stop Recording" : "Record"}
                    description={recording ? "Recording in progress..." : "Tap to start dictation"}
                    onClick={toggleRecording} layout="below" />)}
                {lastTranscription && row(
                    <div style={{ fontSize: "12px", padding: "8px", background: "rgba(0,0,0,0.2)",
                        borderRadius: "4px", fontStyle: "italic" }}>
                        {lastTranscription}
                    </div>)}
            </PanelSection>
            <PanelSection title="Model">
                {!hasModel && row(<div style={{ padding: "8px", fontSize: "12px" }}>
                    Pick a speech model to start dictating. Nothing downloads until you choose one.
                </div>)}
                {hasModel && modelOptions.length > 0 &&
                    dropdown("model", "Transcription Model", modelOptions, selectedModel?.note)}
                {downloadingModel ? (
                    <>
                        {row(<div style={{ padding: "8px", fontSize: "12px" }}>
                            {`Downloading ${downloadingModel.label}: ${modelStatus?.progress_mb ?? 0}`
                                + ` / ${modelStatus?.total_mb ?? 0} MB`}
                        </div>)}
                        {row(<ButtonItem label="Cancel Download" onClick={handleCancelDownload} layout="below" />)}
                    </>
                ) : knownModels?.models.map(modelChoice)}
                {modelStatus?.error && row(<div style={{ padding: "8px", fontSize: "12px", color: "#ff8080" }}>
                    {modelStatus.error}
                </div>)}
                {knownModels && link("Model Files", knownModels.repo)}
                {knownModels && link("Original NVIDIA Model", knownModels.original)}
            </PanelSection>
            <PanelSection title="Trigger">
                {dropdown("trigger_button", "Controller Button", triggerOptions,
                    "Hold to talk. Unmap this button in Steam Input so no game uses it")}
                {row(<ButtonItem label="Press the Button You Want"
                    description={captureText || "Works with any connected controller; hold two buttons for a chord"}
                    onClick={handleCapture} layout="below" />)}
                {dropdown("trigger_mode", "Mode", modeOptions, "Hold: release to stop. Toggle: press again to stop.")}
            </PanelSection>
            <PanelSection title="Audio">
                {dropdown("audio_device", "Input Device", audioOptions)}
                {slider("noise_threshold", "Noise Gate", 0, 0.01, 0.0001, "Ignore audio quieter than this")}
            </PanelSection>
            <PanelSection title="Overlay">
                {toggle("overlay", "Show Overlay", "Show overlay during recording")}
                {dropdown("overlay_position", "Position", overlayPositionOptions, "Where the overlay appears on screen")}
            </PanelSection>
            <PanelSection title="Feedback">
                {toggle("audio_cue", "Audio Cue", "Beep on record start/stop")}
                {toggle("haptic", "Haptic Feedback", "Controller vibration on record start/stop")}
            </PanelSection>
            <PanelSection title="Text Processing">
                {toggle("dictation_commands", "Dictation Commands", "Replace 'period', 'comma', etc. with punctuation")}
                {toggle("auto_capitalize", "Auto-Capitalize", "Capitalize after sentences and newlines")}
                {toggle("undo_enabled", "Undo", "Quick tap to delete last transcription")}
                {dropdown("submit_key", "Send After Dictation", submitKeyOptions, "Key pressed after the text is typed")}
                {dropdown("chat_profile", "Game Chat", chatProfileOptions,
                    "Opens the chat box and turns \"party, ...\" into /p. Profiles are set in config.yaml")}
                {slider("recording_timeout", "Recording Timeout", 0, 120, 5, "Auto-stop after N seconds (0 = no limit)")}
            </PanelSection>
            {PEDALS.map(pedalSection)}
            <PanelSection title="Advanced">
                {text("trigger_key", "Keyboard Shortcut", "Hold to record, e.g. ctrl+shift+space. Empty to disable")}
                {dropdown("backend", "Backend", backendOptions, "Takes effect on restart")}
                {text("model", "Model Path", "Path to a GGUF model file")}
                {slider("history", "History Entries", 0, 100, 5, "Transcriptions to keep")}
                {toggle("tray", "Tray Icon", "Show system tray icon (desktop mode)")}
            </PanelSection>
            <PanelSection title="Troubleshooting">
                {row(<ButtonItem label="Restart Service" description="Stop and restart Whisker"
                    onClick={handleRestart} layout="below" />)}
                {row(<ButtonItem label="Run Diagnostics" description={diagnosticsText} onClick={handleDiagnostics}
                    layout="below" />)}
                {row(<ButtonItem label={showLogs ? "Hide Logs" : "View Logs"} description="Show recent service logs"
                    onClick={handleShowLogs} layout="below" />)}
                {showLogs && row(
                    <div>
                        <pre style={{ fontSize: "10px", maxHeight: "200px", overflow: "auto", whiteSpace: "pre-wrap",
                            wordBreak: "break-all", background: "rgba(0,0,0,0.3)", padding: "8px",
                            borderRadius: "4px" }}>
                            {logs}
                        </pre>
                        <ButtonItem label="Refresh" onClick={handleRefreshLogs} layout="below" />
                    </div>)}
            </PanelSection>
        </>
    );
}

export default definePlugin(() => ({
    name: "Whisker",
    titleView: <div className={staticClasses.Title}>Whisker</div>,
    content: <WhiskerPanel />,
    icon: <FaMicrophone />,
}));
