// The Quick Look preview of a .celsch: Space on a sheet in the Finder shows its circuit,
// drawn by Céline's own canvas (Celine::SheetPicture). Built as an app extension inside the
// Standalone's bundle (CMakeLists.txt); macOS 12 and later, which have data-based previews.

#import <Cocoa/Cocoa.h>
#import <QuickLookUI/QuickLookUI.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "../ui/SheetPicture.h"

namespace
{
    /** JUCE, once, on the main thread: the canvas is a Component and draws on the message
        thread. Kept for the life of the extension's process. */
    void startJuceOnce()
    {
        static auto* juceStarted = new juce::ScopedJuceInitialiser_GUI();
        juce::ignoreUnused (juceStarted);
    }
} // namespace

API_AVAILABLE (macos (12.0))
@interface CelinePreviewProvider : QLPreviewProvider <QLPreviewingController>
@end

@implementation CelinePreviewProvider

- (void) providePreviewForFileRequest: (QLFilePreviewRequest*) request
                    completionHandler: (void (^) (QLPreviewReply* _Nullable, NSError* _Nullable)) handler
{
    NSString* path = request.fileURL.path;
    __block NSData* png = nil;
    __block CGSize size = CGSizeZero;

    // Quick Look asks on a queue of its own; JUCE draws on the main thread.
    void (^render) (void) = ^{
        startJuceOnce();

        juce::MemoryBlock picture;
        int width = 0, height = 0;

        if (Celine::SheetPicture::previewPng (juce::File (juce::String::fromUTF8 (path.UTF8String)), picture, width, height))
        {
            png = [NSData dataWithBytes: picture.getData() length: picture.getSize()];
            size = CGSizeMake (width, height);
        }
    };

    if (NSThread.isMainThread)
        render();
    else
        dispatch_sync (dispatch_get_main_queue(), render);

    if (png == nil)
    {
        handler (nil, [NSError errorWithDomain: NSCocoaErrorDomain code: NSFileReadCorruptFileError userInfo: nil]);
        return;
    }

    NSData* data = png;
    handler ([[QLPreviewReply alloc] initWithDataOfContentType: UTTypePNG
                                                    contentSize: size
                                              dataCreationBlock: ^NSData* _Nullable (QLPreviewReply*, NSError**) { return data; }],
             nil);
}

@end
