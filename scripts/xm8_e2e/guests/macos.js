// JXA uses public window metadata; no Accessibility or credential input is needed.
ObjC.import('AppKit');
ObjC.import('CoreGraphics');

function run(argv) {
    var action = argv[0], executable = argv[1];
    if (!executable || executable[0] !== '/') throw Error('Pinned executable required');
    var apps = ObjC.unwrap($.NSWorkspace.sharedWorkspace.runningApplications).filter(function(app) {
        return app.executableURL && ObjC.unwrap(app.executableURL.path) === executable;
    });
    if (action === 'stop' && apps.length === 0) return JSON.stringify({stopped:true});
    if (apps.length !== 1) throw Error('Expected one pinned test application');
    var app = apps[0], pid = Number(app.processIdentifier);
    if (action === 'stop') {
        if (!app.terminate) throw Error('Graceful termination unavailable');
        // NSRunningApplication properties update through the run loop; a sleep
        // alone can leave isTerminated stale after the process has exited.
        for (var i=0; i<200 && !app.terminated; i++) {
            $.NSRunLoop.currentRunLoop.runUntilDate($.NSDate.dateWithTimeIntervalSinceNow(0.1));
        }
        if (!app.terminated) throw Error('Application did not terminate normally');
        return JSON.stringify({stopped:true,pid:pid});
    }
    if (action === 'activate') {
        app.activateWithOptions(3);
        $.NSThread.sleepForTimeInterval(0.3);
    } else if (action === 'finder') {
        var finders = ObjC.unwrap($.NSRunningApplication.runningApplicationsWithBundleIdentifier('com.apple.finder'));
        if (finders.length !== 1) throw Error('Focus helper unavailable');
        finders[0].activateWithOptions(3);
        $.NSThread.sleepForTimeInterval(0.3);
        return JSON.stringify({left:Number($.NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier) === Number(finders[0].processIdentifier),app_pid:pid});
    } else if (action !== 'window') throw Error('Unknown action');
    var windows = ObjC.deepUnwrap(ObjC.castRefToObject($.CGWindowListCopyWindowInfo(1,0))).filter(function(w) {
        return w.kCGWindowOwnerPID === pid && w.kCGWindowLayer === 0 && w.kCGWindowBounds.Width > 100 && w.kCGWindowBounds.Height > 100;
    });
    if (windows.length !== 1) throw Error('Expected one visible test window');
    var b = windows[0].kCGWindowBounds;
    var screen = $.NSScreen.mainScreen.frame.size;
    return JSON.stringify({pid:pid,start_time:Number(app.launchDate.timeIntervalSince1970),
        focused:Number($.NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier) === pid,
        rect:{Left:b.X,Top:b.Y,Right:b.X+b.Width,Bottom:b.Y+b.Height},screen:[screen.width,screen.height]});
}
