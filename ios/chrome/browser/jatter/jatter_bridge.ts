// ios/chrome/browser/jatter/jatter_bridge.ts

(window as any).sendAuthToken = function(token: string) {
    const win = window as any;
    if (win.webkit && win.webkit.messageHandlers && win.webkit.messageHandlers.jatterAuth) {
        win.webkit.messageHandlers.jatterAuth.postMessage({
            command: 'jatterAuth.sendToken',
            token: token
        });
    }
};

(window as any).sendPrivateKey = function(privateKey: string) {
    const win = window as any;
    if (win.webkit && win.webkit.messageHandlers && win.webkit.messageHandlers.jatterAuth) {
        win.webkit.messageHandlers.jatterAuth.postMessage({
            command: 'jatterAuth.sendPrivateKey',
            privateKey: privateKey
        });
    }
};