/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

#include <atomic>
#include <cstdint>
#include <iostream>

namespace tracktion { inline namespace engine
{

static Engine* instance = nullptr;
static juce::Array<Engine*> engines;

namespace
{
    bool isTruthyEnvFlag (const char* name)
    {
        if (name == nullptr)
            return false;

        const auto value = juce::SystemStats::getEnvironmentVariable (name, {}).trim();
        return value == "1" || value.equalsIgnoreCase ("true") || value.equalsIgnoreCase ("yes");
    }

    bool shouldTraceTracktionEngine()
    {
        static const bool enabled = isTruthyEnvFlag ("BATCHY_TRACKTION_ENGINE_TRACE");
        return enabled;
    }

   #if JUCE_DEBUG || JUCE_UNIT_TESTS
    std::atomic<int> liveEngineCount { 0 };

    void logEngineLifecycleEvent (const char* action, const Engine* engine, int remaining)
    {
        if (! shouldTraceTracktionEngine())
            return;

        const auto pointerValue = static_cast<juce::uint64> (reinterpret_cast<uintptr_t> (engine));
        juce::String message (juce::String ("[TracktionEngine] Engine ")
                                + action
                                + " (ptr=0x"
                                + juce::String::toHexString (pointerValue)
                                + ") remaining engines: "
                                + juce::String (remaining));

        juce::Logger::writeToLog (message);
        std::cout << message << std::endl;
    }
   #endif
}

Engine::Engine (std::unique_ptr<PropertyStorage> ps, std::unique_ptr<UIBehaviour> ub, std::unique_ptr<EngineBehaviour> eb)
{
    instance = this;
    engines.add (this);

   #if JUCE_DEBUG || JUCE_UNIT_TESTS
    {
        const auto liveCount = liveEngineCount.fetch_add (1, std::memory_order_relaxed) + 1;
        logEngineLifecycleEvent ("created", this, liveCount);
    }
   #endif

    jassert (ps != nullptr);
    propertyStorage = std::move (ps);

    if (ub == nullptr)
        uiBehaviour = std::make_unique<UIBehaviour>();
    else
        uiBehaviour = std::move (ub);

    if (! eb)
        engineBehaviour = std::make_unique<EngineBehaviour>();
    else
        engineBehaviour = std::move (eb);

    initialise();
}

Engine::Engine (juce::String applicationName, std::unique_ptr<UIBehaviour> ub, std::unique_ptr<EngineBehaviour> eb)
    : Engine (std::make_unique<PropertyStorage> (applicationName), std::move (ub), std::move (eb))
{
}

Engine::Engine (juce::String applicationName)  : Engine (applicationName, nullptr, nullptr)
{
}

void Engine::initialise()
{
    const auto logInitStep = [](const char* message)
    {
        if (! shouldTraceTracktionEngine())
            return;

        const juce::String msg (message);
        juce::Logger::writeToLog (msg);
        std::cout << msg << std::endl;
    };

    logInitStep ("[TracktionEngine] Engine::initialise begin");

    Selectable::initialise();
    AudioScratchBuffer::initialise();

    logInitStep ("[TracktionEngine] Engine::initialise scratch buffers ready");

    projectManager             = std::make_unique<ProjectManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise projectManager constructed");
    activeEdits                = std::unique_ptr<ActiveEdits> (new ActiveEdits());
    logInitStep ("[TracktionEngine] Engine::initialise activeEdits constructed");
    temporaryFileManager       = std::make_unique<TemporaryFileManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise temporaryFileManager constructed");
    recordingThumbnailManager  = std::make_unique<RecordingThumbnailManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise recordingThumbnailManager constructed");
    waveInputRecordingThread   = std::make_unique<WaveInputRecordingThread> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise waveInputRecordingThread constructed");
    editDeleter                = std::unique_ptr<EditDeleter> (new EditDeleter());
    logInitStep ("[TracktionEngine] Engine::initialise editDeleter constructed");
    audioFileFormatManager     = std::make_unique<AudioFileFormatManager>();
    logInitStep ("[TracktionEngine] Engine::initialise audioFileFormatManager constructed");
    midiLearnState             = std::make_unique<MidiLearnState> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise midiLearnState constructed");
    renderManager              = std::make_unique<RenderManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise renderManager constructed");
    audioFileManager           = std::make_unique<AudioFileManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise audioFileManager constructed");
    const bool hardwareAccess = engineBehaviour->allowDeviceManagerHardwareAccess();
    deviceManager              = std::unique_ptr<DeviceManager> (new DeviceManager (*this, hardwareAccess));
    logInitStep (hardwareAccess
                    ? "[TracktionEngine] Engine::initialise deviceManager constructed"
                    : "[TracktionEngine] Engine::initialise deviceManager constructed (hardware disabled)");
    midiProgramManager         = std::make_unique<MidiProgramManager> (*this);
    logInitStep ("[TracktionEngine] Engine::initialise midiProgramManager constructed");
    externalControllerManager  = std::unique_ptr<ExternalControllerManager> (new ExternalControllerManager (*this));
    logInitStep ("[TracktionEngine] Engine::initialise externalControllerManager constructed");
    backgroundJobManager       = std::make_unique<BackgroundJobManager>();
    logInitStep ("[TracktionEngine] Engine::initialise backgroundJobManager constructed");
    pluginManager              = std::make_unique<PluginManager> (*this);

    logInitStep ("[TracktionEngine] Engine::initialise core managers constructed");

    if (engineBehaviour->autoInitialiseDeviceManager())
    {
        logInitStep ("[TracktionEngine] Engine::initialise auto device manager init starting");
        deviceManager->initialise (getEngineBehaviour().shouldOpenAudioInputByDefault()
                                        ? DeviceManager::defaultNumChannelsToOpen : 0,
                                   DeviceManager::defaultNumChannelsToOpen);
        logInitStep ("[TracktionEngine] Engine::initialise device manager initialised");
    }

    pluginManager->initialise();
    logInitStep ("[TracktionEngine] Engine::initialise plugin manager ready");

    getProjectManager().initialise();
    logInitStep ("[TracktionEngine] Engine::initialise project manager initialised");

    externalControllerManager->initialise();
    logInitStep ("[TracktionEngine] Engine::initialise external controller manager initialised");

    logInitStep ("[TracktionEngine] Engine::initialise complete");
}

Engine::~Engine()
{
    // First make sure to clear any edits that are in line to be deleted
    editDeleter.reset();

    getProjectManager().saveList();

    getExternalControllerManager().shutdown();
    getDeviceManager().closeDevices();
    getBackgroundJobs().stopAndDeleteAllRunningJobs();

    temporaryFileManager->cleanUp();

    getRenderManager().cleanUp();
    backgroundJobManager.reset();
    deviceManager.reset();
    midiProgramManager.reset();

    MelodyneFileReader::cleanUpOnShutdown();
    pluginManager.reset();

    temporaryFileManager.reset();
    projectManager.reset();

    renderManager.reset();
    externalControllerManager.reset();
    propertyStorage.reset();
    uiBehaviour.reset();
    engineBehaviour.reset();
    audioFileManager.reset();
    midiLearnState.reset();
    audioFileFormatManager.reset();
    backToArrangerUpdateTimer.reset();
    bufferedAudioFileManager.reset();

    instance = nullptr;
    engines.removeFirstMatchingValue (this);

   #if JUCE_DEBUG || JUCE_UNIT_TESTS
    {
        const auto liveCount = liveEngineCount.fetch_sub (1, std::memory_order_relaxed) - 1;
        logEngineLifecycleEvent ("destroyed", this, liveCount);
    }
   #endif
}

juce::String Engine::getVersion()
{
    return "Tracktion Engine v3.1.0";
}

juce::Array<Engine*> Engine::getEngines()
{
    return engines;
}

#if TRACKTION_ENABLE_SINGLETONS
Engine& Engine::getInstance()
{
    jassert (instance != nullptr);
    return *instance;
}
#endif

ProjectManager& Engine::getProjectManager() const
{
    jassert (projectManager != nullptr);
    return *projectManager;
}

AudioFileFormatManager& Engine::getAudioFileFormatManager() const
{
    jassert (audioFileFormatManager != nullptr);
    return *audioFileFormatManager;
}

DeviceManager& Engine::getDeviceManager() const
{
    jassert (deviceManager != nullptr);
    return *deviceManager;
}

MidiProgramManager& Engine::getMidiProgramManager() const
{
    jassert (midiProgramManager);
    return *midiProgramManager;
}

ExternalControllerManager& Engine::getExternalControllerManager() const
{
    jassert (externalControllerManager != nullptr);
    return *externalControllerManager;
}

RenderManager& Engine::getRenderManager() const
{
    jassert (renderManager != nullptr);
    return *renderManager;
}

BackgroundJobManager& Engine::getBackgroundJobs() const
{
    jassert (backgroundJobManager != nullptr);
    return *backgroundJobManager;
}

PropertyStorage& Engine::getPropertyStorage() const
{
    jassert (propertyStorage != nullptr);
    return *propertyStorage;
}

UIBehaviour& Engine::getUIBehaviour() const
{
    jassert (uiBehaviour != nullptr);
    return *uiBehaviour;
}

EngineBehaviour& Engine::getEngineBehaviour() const
{
    jassert (engineBehaviour != nullptr);
    return *engineBehaviour;
}

AudioFileManager& Engine::getAudioFileManager() const
{
    jassert (audioFileManager != nullptr);
    return *audioFileManager;
}

MidiLearnState& Engine::getMidiLearnState() const
{
    jassert (midiLearnState != nullptr);
    return *midiLearnState;
}

PluginManager& Engine::getPluginManager() const
{
    jassert (pluginManager != nullptr);
    return *pluginManager;
}

EditDeleter& Engine::getEditDeleter() const
{
    jassert (editDeleter != nullptr);
    return *editDeleter;
}

TemporaryFileManager& Engine::getTemporaryFileManager() const
{
    jassert (temporaryFileManager != nullptr);
    return *temporaryFileManager;
}

RecordingThumbnailManager& Engine::getRecordingThumbnailManager() const
{
    jassert (recordingThumbnailManager != nullptr);
    return *recordingThumbnailManager;
}

WaveInputRecordingThread& Engine::getWaveInputRecordingThread() const
{
    jassert (waveInputRecordingThread != nullptr);
    return *waveInputRecordingThread;
}

ActiveEdits& Engine::getActiveEdits() const noexcept
{
    jassert (activeEdits != nullptr);
    return *activeEdits;
}

GrooveTemplateManager& Engine::getGrooveTemplateManager()
{
    if (! grooveTemplateManager)
        grooveTemplateManager = std::make_unique<GrooveTemplateManager> (*this);

    return *grooveTemplateManager;
}

CompFactory& Engine::getCompFactory() const
{
    if (! compFactory)
        compFactory = std::make_unique<CompFactory>();

    return *compFactory;
}

WarpTimeFactory& Engine::getWarpTimeFactory() const
{
    if (! warpTimeFactory)
        warpTimeFactory = std::make_unique<WarpTimeFactory>();

    return *warpTimeFactory;
}

SharedTimer& Engine::getBackToArrangerUpdateTimer() const
{
    if (! backToArrangerUpdateTimer)
        backToArrangerUpdateTimer = std::make_unique<SharedTimer> (HertzTag_t, 10);

    return *backToArrangerUpdateTimer;
}

BufferedAudioFileManager& Engine::getBufferedAudioFileManager()
{
    if (! bufferedAudioFileManager)
        bufferedAudioFileManager = std::make_unique<BufferedAudioFileManager> (*this);

    return *bufferedAudioFileManager;
}

bool EngineBehaviour::shouldLoadPlugin (ExternalPlugin& p)
{
    return p.edit.shouldLoadPlugins();
}

}} // namespace tracktion { inline namespace engine
