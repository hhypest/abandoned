@{
    EngineBase = '260bb2e1c5610b31c63a36206eedd289409c5f11'
    EngineVersion = '5.7.4'
    DotNet = '8.0.412'
    VCTools = '14.44.35207'
    Compiler = '14.44.35230'
    WindowsSDK = '10.0.26100.0'
    # Effective Git blobs after applying the four project patches to EngineBase.
    EngineFiles = @{
        'Engine/Source/Runtime/Renderer/Private/SceneTextures.cpp' = 'd57609e14fe9e513bbd772e9a958f5f88d54d82a'
        'Engine/Source/Runtime/AudioMixer/Private/AudioMixerDevice.cpp' = '1cfab2cbb184b238d58005de64aa7c989d4f0da3'
        'Engine/Source/Runtime/CoreUObject/Private/UObject/GarbageCollection.cpp' = '0d19301f2f6c3117f4eccda94e7454544261ad7f'
        'Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp' = 'ff5832b71f2663eb4dfa83421a4ca885b5e09a32'
    }
}
