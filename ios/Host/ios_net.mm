// The iPhone transport: hostnet::hostHttpNative on URLSession (see net_portable.h, NET.md).
// Synchronous with a semaphore, so never call it on the main thread. TLS is the system's (ATS
// defaults); a request with a pin accepts only the server certificate with that SHA-256 (add-ons
// talking to a device's self-signed certificate). Cookies are never stored or sent by the system.
#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>
#import <Security/Security.h>

#include <cstring>
#include <string>

#include "net_portable.h"

using bbw::hostnet::HttpRequest;
using bbw::hostnet::HttpResult;

namespace {

void say(char *problem, size_t size, const char *text) {
  if (problem && size) {
    strncpy(problem, text, size - 1);
    problem[size - 1] = '\0';
  }
}

// 64 hex digits with optional colons, any case, to 32 bytes.
bool parsePin(const char *text, uint8_t out[32]) {
  size_t n = 0;
  int high = -1;
  for (const char *p = text; p && *p; ++p) {
    int v;
    if (*p >= '0' && *p <= '9') v = *p - '0';
    else if (*p >= 'a' && *p <= 'f') v = *p - 'a' + 10;
    else if (*p >= 'A' && *p <= 'F') v = *p - 'A' + 10;
    else if (*p == ':' || *p == ' ') continue;
    else return false;
    if (high < 0) high = v;
    else {
      if (n >= 32) return false;
      out[n++] = static_cast<uint8_t>(high * 16 + v);
      high = -1;
    }
  }
  return n == 32 && high < 0;
}

const char *explain(NSError *error) {
  if ([error.domain isEqualToString:NSURLErrorDomain]) {
    switch (error.code) {
      case NSURLErrorTimedOut: return "timed out";
      case NSURLErrorCannotFindHost:
      case NSURLErrorDNSLookupFailed: return "the name could not be found";
      case NSURLErrorCannotConnectToHost: return "could not connect";
      case NSURLErrorNotConnectedToInternet: return "no network connection";
      case NSURLErrorNetworkConnectionLost: return "the connection was lost";
      case NSURLErrorAppTransportSecurityRequiresSecureConnection:
        return "plain http is allowed only to local network addresses";
      case NSURLErrorSecureConnectionFailed:
      case NSURLErrorServerCertificateUntrusted:
      case NSURLErrorServerCertificateHasBadDate:
      case NSURLErrorServerCertificateHasUnknownRoot:
      case NSURLErrorServerCertificateNotYetValid:
      case NSURLErrorClientCertificateRejected: return "the secure connection could not be made";
      case NSURLErrorCancelled: return "stopped";
      default: break;
    }
  }
  return "the request failed";
}

}  // namespace

// One request's delegate: collects the body up to the cap, pins, refuses redirects when told to.
@interface BBWTask : NSObject <NSURLSessionDataDelegate>
@property(nonatomic) NSMutableData *data;
@property(nonatomic) size_t cap;
@property(nonatomic) BOOL truncated;
@property(nonatomic) BOOL stoppedEarly;
@property(nonatomic) BOOL follow;
@property(nonatomic) BOOL pinned;
@property(nonatomic) NSData *pinDigest;
@property(nonatomic) NSString *stopAfter;
@property(nonatomic) NSHTTPURLResponse *response;
@property(nonatomic) NSError *error;
@property(nonatomic) BOOL pinFailed;
@property(nonatomic) dispatch_semaphore_t done;
@end

@implementation BBWTask

- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                    newRequest:(NSURLRequest *)request
             completionHandler:(void (^)(NSURLRequest *))completion {
  if (self.follow) {
    // Keep the Cookie/Authorization headers off a redirect to another host.
    if (![request.URL.host isEqualToString:task.originalRequest.URL.host]) {
      NSMutableURLRequest *copy = [request mutableCopy];
      [copy setValue:nil forHTTPHeaderField:@"Authorization"];
      [copy setValue:nil forHTTPHeaderField:@"Cookie"];
      completion(copy);
    } else {
      completion(request);
    }
  } else {
    self.response = response;
    completion(nil);  // not followed: the 3xx is the answer
  }
}

- (void)URLSession:(NSURLSession *)session
                   task:(NSURLSessionTask *)task
    didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
      completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))completion {
  if (self.pinned && [challenge.protectionSpace.authenticationMethod isEqualToString:NSURLAuthenticationMethodServerTrust]) {
    SecTrustRef trust = challenge.protectionSpace.serverTrust;
    NSData *leaf = nil;
    if (trust && SecTrustGetCertificateCount(trust) > 0) {
      SecCertificateRef cert = nullptr;
#if __IPHONE_OS_VERSION_MIN_REQUIRED >= 150000
      CFArrayRef chain = SecTrustCopyCertificateChain(trust);
      if (chain && CFArrayGetCount(chain) > 0) cert = (SecCertificateRef)CFRetain(CFArrayGetValueAtIndex(chain, 0));
      if (chain) CFRelease(chain);
#else
      cert = (SecCertificateRef)CFRetain(SecTrustGetCertificateAtIndex(trust, 0));
#endif
      if (cert) {
        leaf = CFBridgingRelease(SecCertificateCopyData(cert));
        CFRelease(cert);
      }
    }
    uint8_t digest[CC_SHA256_DIGEST_LENGTH] = {};
    if (leaf) CC_SHA256(leaf.bytes, (CC_LONG)leaf.length, digest);
    if (leaf && [self.pinDigest isEqualToData:[NSData dataWithBytes:digest length:sizeof(digest)]]) {
      completion(NSURLSessionAuthChallengeUseCredential, [NSURLCredential credentialForTrust:trust]);
    } else {
      self.pinFailed = YES;
      completion(NSURLSessionAuthChallengeCancelAuthenticationChallenge, nil);
    }
    return;
  }
  completion(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}

- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition))completion {
  if ([response isKindOfClass:[NSHTTPURLResponse class]]) self.response = (NSHTTPURLResponse *)response;
  completion(NSURLSessionResponseAllow);
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)chunk {
  const size_t room = self.cap > 0 ? self.cap - 1 - self.data.length : 0;
  if (chunk.length > room) {
    [self.data appendBytes:chunk.bytes length:room];
    self.truncated = YES;
    [task cancel];
    return;
  }
  [self.data appendData:chunk];
  if (self.stopAfter.length) {
    // Looked for in the part just read and a little before it.
    const NSUInteger back = chunk.length + self.stopAfter.length + 8;
    const NSUInteger from = self.data.length > back ? self.data.length - back : 0;
    NSData *tail = [self.data subdataWithRange:NSMakeRange(from, self.data.length - from)];
    NSString *text = [[NSString alloc] initWithData:tail encoding:NSISOLatin1StringEncoding];
    if ([text rangeOfString:self.stopAfter options:NSCaseInsensitiveSearch].location != NSNotFound) {
      self.stoppedEarly = YES;
      [task cancel];
    }
  }
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
  self.error = error;
  dispatch_semaphore_signal(self.done);
}

@end

namespace bbw {
namespace hostnet {

int hostHttpNative(const HttpRequest &request, char *response, size_t cap, HttpResult &result,
                   char *problem, size_t problemSize) {
  if (response && cap) response[0] = '\0';
  if ([NSThread isMainThread]) {  // would stall the interface, and would deadlock if the queue is main
    say(problem, problemSize, "network requests must not run on the main thread");
    return -1;
  }
  @autoreleasepool {
    NSURL *url = [NSURL URLWithString:[NSString stringWithUTF8String:request.url]];
    if (!url || !url.host) {
      say(problem, problemSize, "the address could not be used");
      return -2;
    }
    const double timeoutS = request.timeoutMs > 0 ? request.timeoutMs / 1000.0 : 8.0;
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url
                                                       cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                   timeoutInterval:timeoutS];
    req.HTTPMethod = [NSString stringWithUTF8String:request.method ? request.method : "GET"];
    req.HTTPShouldHandleCookies = NO;
    if (request.headers) {
      NSString *all = [NSString stringWithUTF8String:request.headers];
      for (NSString *line in [all componentsSeparatedByString:@"\r\n"]) {
        const NSRange colon = [line rangeOfString:@":"];
        if (colon.location == NSNotFound || colon.location == 0) continue;
        NSString *name = [line substringToIndex:colon.location];
        NSString *value = [[line substringFromIndex:colon.location + 1]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        [req setValue:value forHTTPHeaderField:name];
      }
    }
    if (request.body && request.bodyLen) req.HTTPBody = [NSData dataWithBytes:request.body length:request.bodyLen];

    BBWTask *task = [BBWTask new];
    task.data = [NSMutableData data];
    task.cap = cap;
    task.follow = request.followRedirects;
    task.done = dispatch_semaphore_create(0);
    task.stopAfter = request.stopAfter ? [NSString stringWithUTF8String:request.stopAfter] : nil;
    if (request.pinSha256 && request.pinSha256[0]) {
      uint8_t pin[32];
      if (!parsePin(request.pinSha256, pin)) {
        say(problem, problemSize, "the certificate is not the one expected");
        return -3;
      }
      task.pinned = YES;
      task.pinDigest = [NSData dataWithBytes:pin length:32];
    }

    NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    config.HTTPCookieStorage = nil;
    config.HTTPShouldSetCookies = NO;
    config.URLCredentialStorage = nil;
    config.timeoutIntervalForRequest = timeoutS;
    config.timeoutIntervalForResource = timeoutS;
    config.waitsForConnectivity = NO;
    // Local network addresses answer or fail at once; the system's own wait would only hide a refusal.
    NSOperationQueue *queue = [NSOperationQueue new];
    queue.maxConcurrentOperationCount = 1;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:config delegate:task delegateQueue:queue];
    NSURLSessionDataTask *dataTask = [session dataTaskWithRequest:req];
    [dataTask resume];
    // The session's own time-outs apply; the wait is a little longer as a backstop.
    const long waited = dispatch_semaphore_wait(task.done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)((timeoutS + 2) * NSEC_PER_SEC)));
    if (waited != 0) {
      [dataTask cancel];
      [session invalidateAndCancel];
      say(problem, problemSize, "timed out");
      return -1;
    }
    [session finishTasksAndInvalidate];

    const bool cutOnPurpose = task.truncated || task.stoppedEarly;
    if (task.error && !cutOnPurpose) {
      if (task.pinFailed) {
        say(problem, problemSize, "the certificate is not the one expected");
        return -3;
      }
      say(problem, problemSize, explain(task.error));
      return -1;
    }
    NSHTTPURLResponse *http = task.response;
    if (!http) {
      say(problem, problemSize, "no answer");
      return -1;
    }
    const size_t n = task.data.length;
    if (response && cap) {
      memcpy(response, task.data.bytes, n);
      response[n] = '\0';
    }
    result.length = n;
    result.truncated = task.truncated;
    NSDictionary *fields = http.allHeaderFields;
    NSString *location = fields[@"Location"];
    if (location) {
      strncpy(result.location, location.UTF8String, sizeof(result.location) - 1);
    }
    // Several Set-Cookie headers arrive joined; the first one's name=value is what the board keeps.
    NSString *cookie = fields[@"Set-Cookie"];
    if (cookie) {
      NSString *first = [[cookie componentsSeparatedByString:@";"] firstObject];
      strncpy(result.setCookie, first.UTF8String, sizeof(result.setCookie) - 1);
    }
    return (int)http.statusCode;
  }
}

}  // namespace hostnet
}  // namespace bbw
