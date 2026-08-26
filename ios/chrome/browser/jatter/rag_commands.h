#ifndef IOS_CHROME_BROWSER_JATTER_RAG_COMMANDS_H_
#define IOS_CHROME_BROWSER_JATTER_RAG_COMMANDS_H_

#import <Foundation/Foundation.h>

// Block signature for returning the user's decision
typedef void (^RagPermissionDecisionBlock)(BOOL allowed);

@protocol RagCommands <NSObject>
// Shows the permission UI (Bottom Sheet) for a specific host.
- (void)showRagPermissionUIForHost:(NSString*)host
                   decisionHandler:(RagPermissionDecisionBlock)decisionHandler;
- (void)showRagManagementUIForHost:(NSString*)host 
                          siteName:(NSString*)siteName 
                         isEnabled:(BOOL)isEnabled;
@end

// Handled by LocationBarCoordinator (for the Omnibox Icon)
@protocol RagLocationBarCommands <NSObject>
- (void)updateRagLocationBarIcon;
- (void)jatterIconTapped;
@end

#endif  // IOS_CHROME_BROWSER_JATTER_RAG_COMMANDS_H_