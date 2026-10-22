// CosmicFlux AUv3 audio effect (aufx). Pure Objective-C interface so it can be
// imported from Swift; the implementation is Objective-C++ and owns the C++ DSP.
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface CosmicFluxAudioUnit : AUAudioUnit

- (instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                     options:(AudioComponentInstantiationOptions)options
                                       error:(NSError **)outError NS_DESIGNATED_INITIALIZER;

@end

NS_ASSUME_NONNULL_END
