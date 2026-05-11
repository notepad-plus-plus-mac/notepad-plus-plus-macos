#import "NppLangsManager.h"

// ── NppLangDef ───────────────────────────────────────────────────────────────

@implementation NppLangDef
- (instancetype)init {
    self = [super init];
    if (self) {
        _name = @"";
        _extensions = @"";
        _commentLine = @"";
        _commentStart = @"";
        _commentEnd = @"";
        _tabSettings = -1;
        _keywords = [NSMutableDictionary new];
    }
    return self;
}
@end

// ── NppLangsManager ──────────────────────────────────────────────────────────

@implementation NppLangsManager {
    NSMutableArray<NppLangDef *> *_langs;
    NSMutableDictionary<NSString *, NppLangDef *> *_langsByName;
    NSMutableDictionary<NSString *, NSString *> *_extMap; // ext → lang name
}

+ (instancetype)shared {
    static NppLangsManager *instance;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ instance = [[NppLangsManager alloc] init]; });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _langs = [NSMutableArray new];
        _langsByName = [NSMutableDictionary new];
        _extMap = [NSMutableDictionary new];
    }
    return self;
}

#pragma mark - Loading

- (void)loadLangs {
    [_langs removeAllObjects];
    [_langsByName removeAllObjects];
    [_extMap removeAllObjects];

    // Try user langs.xml first, fall back to bundled langs.model.xml
    NSString *userPath = [NSHomeDirectory() stringByAppendingPathComponent:@".notepad++/langs.xml"];
    NSData *data = [[NSFileManager defaultManager] fileExistsAtPath:userPath]
                   ? [NSData dataWithContentsOfFile:userPath] : nil;
    if (!data) {
        NSString *bundlePath = [[NSBundle mainBundle] pathForResource:@"langs.model" ofType:@"xml"];
        if (bundlePath) data = [NSData dataWithContentsOfFile:bundlePath];
    }
    if (!data) {
        NSLog(@"[Langs] No langs.xml or langs.model.xml found");
        return;
    }

    // Use raw string parsing to preserve attribute values (same approach as functionList)
    NSString *raw = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
    if (!raw) return;

    [self _parseRaw:raw];

    NSLog(@"[Langs] Loaded %lu language definitions, %lu extension mappings",
          (unsigned long)_langs.count, (unsigned long)_extMap.count);
}

/// Parse the raw XML string, extracting <Language> elements and their children.
- (void)_parseRaw:(NSString *)raw {
    NSUInteger len = raw.length;
    NSUInteger i = 0;

    while (i < len) {
        // Find next <Language
        NSRange langTag = [raw rangeOfString:@"<Language " options:0 range:NSMakeRange(i, len - i)];
        if (langTag.location == NSNotFound) break;
        i = NSMaxRange(langTag);

        // Extract attributes from the <Language ...> tag
        NppLangDef *lang = [[NppLangDef alloc] init];

        // Find the end of the opening tag (> or />)
        BOOL selfClosing = NO;
        NSUInteger tagEnd = i;
        BOOL inQuote = NO;
        unichar quoteChar = 0;
        while (tagEnd < len) {
            unichar c = [raw characterAtIndex:tagEnd];
            if (inQuote) {
                if (c == quoteChar) inQuote = NO;
            } else {
                if (c == '"' || c == '\'') { inQuote = YES; quoteChar = c; }
                else if (c == '/') { selfClosing = YES; }
                else if (c == '>') { tagEnd++; break; }
            }
            tagEnd++;
        }

        NSString *tagStr = [raw substringWithRange:NSMakeRange(NSMaxRange(langTag) - 9, tagEnd - NSMaxRange(langTag) + 9)];

        // Extract attributes
        lang.name = [self _attrValue:@"name" from:tagStr] ?: @"";
        lang.extensions = [self _attrValue:@"ext" from:tagStr] ?: @"";
        lang.commentLine = [self _attrValue:@"commentLine" from:tagStr] ?: @"";
        lang.commentStart = [self _attrValue:@"commentStart" from:tagStr] ?: @"";
        lang.commentEnd = [self _attrValue:@"commentEnd" from:tagStr] ?: @"";

        // Decode XML entities in comment delimiters. commentLine carries the
        // same entity-encoded payloads (e.g. VB stores commentLine="&apos;");
        // skipping it here caused "Toggle Single Line Comment" to insert the
        // literal string "&apos;" instead of the apostrophe.
        lang.commentLine = [self _decodeEntities:lang.commentLine];
        lang.commentStart = [self _decodeEntities:lang.commentStart];
        lang.commentEnd = [self _decodeEntities:lang.commentEnd];

        NSString *tabStr = [self _attrValue:@"tabSettings" from:tagStr];
        lang.tabSettings = tabStr ? tabStr.integerValue : -1;

        NSString *excludeStr = [self _attrValue:@"exclude" from:tagStr];
        lang.exclude = [excludeStr.lowercaseString isEqualToString:@"yes"];

        if (!lang.name.length) { i = tagEnd; continue; }

        // If not self-closing, parse <Keywords> children until </Language>
        if (!selfClosing) {
            NSRange langClose = [raw rangeOfString:@"</Language>" options:0
                                             range:NSMakeRange(tagEnd, len - tagEnd)];
            NSUInteger bodyEnd = (langClose.location != NSNotFound) ? langClose.location : len;
            NSString *body = [raw substringWithRange:NSMakeRange(tagEnd, bodyEnd - tagEnd)];

            [self _parseKeywords:body into:lang];

            i = (langClose.location != NSNotFound) ? NSMaxRange(langClose) : bodyEnd;
        } else {
            i = tagEnd;
        }

        [_langs addObject:lang];
        _langsByName[lang.name.lowercaseString] = lang;

        // Build extension map
        NSArray *exts = [lang.extensions componentsSeparatedByCharactersInSet:
                         [NSCharacterSet whitespaceCharacterSet]];
        for (NSString *ext in exts) {
            NSString *trimmed = [ext stringByTrimmingCharactersInSet:
                                 [NSCharacterSet whitespaceAndNewlineCharacterSet]];
            if (trimmed.length) _extMap[trimmed.lowercaseString] = lang.name;
        }
    }
}

/// Extract keyword entries from the body between <Language> and </Language>.
- (void)_parseKeywords:(NSString *)body into:(NppLangDef *)lang {
    NSUInteger len = body.length;
    NSUInteger i = 0;

    while (i < len) {
        NSRange kwTag = [body rangeOfString:@"<Keywords " options:0 range:NSMakeRange(i, len - i)];
        if (kwTag.location == NSNotFound) break;
        i = NSMaxRange(kwTag);

        // Find the name attribute
        NSUInteger tagSearchEnd = MIN(i + 200, len);
        NSString *tagPart = [body substringWithRange:NSMakeRange(kwTag.location, tagSearchEnd - kwTag.location)];
        NSString *kwName = [self _attrValue:@"name" from:tagPart];
        if (!kwName.length) continue;

        // Find > that ends the opening tag
        NSUInteger gtPos = [body rangeOfString:@">" options:0 range:NSMakeRange(i, len - i)].location;
        if (gtPos == NSNotFound) break;

        // Check if self-closing
        if (gtPos > 0 && [body characterAtIndex:gtPos - 1] == '/') {
            // Self-closing <Keywords name="..." /> — empty keywords
            i = gtPos + 1;
            continue;
        }

        // Content between > and </Keywords>
        NSUInteger contentStart = gtPos + 1;
        NSRange closeTag = [body rangeOfString:@"</Keywords>" options:0
                                         range:NSMakeRange(contentStart, len - contentStart)];
        if (closeTag.location == NSNotFound) break;

        NSString *content = [body substringWithRange:
                             NSMakeRange(contentStart, closeTag.location - contentStart)];
        content = [content stringByTrimmingCharactersInSet:
                   [NSCharacterSet whitespaceAndNewlineCharacterSet]];

        if (content.length) {
            lang.keywords[kwName] = content;
        }

        i = NSMaxRange(closeTag);
    }
}

/// Extract an attribute value from a tag string, preserving content.
- (nullable NSString *)_attrValue:(NSString *)attrName from:(NSString *)tag {
    NSString *search = [NSString stringWithFormat:@"%@=\"", attrName];
    NSRange r = [tag rangeOfString:search];
    if (r.location == NSNotFound) {
        // Try single quotes
        search = [NSString stringWithFormat:@"%@='", attrName];
        r = [tag rangeOfString:search];
        if (r.location == NSNotFound) return nil;
    }
    unichar quote = [search characterAtIndex:search.length - 1];
    NSUInteger start = NSMaxRange(r);
    NSUInteger end = start;
    while (end < tag.length && [tag characterAtIndex:end] != quote) end++;
    return [tag substringWithRange:NSMakeRange(start, end - start)];
}

/// Decode XML entities: &lt; &gt; &amp; &quot; &apos;
- (NSString *)_decodeEntities:(NSString *)s {
    if (![s containsString:@"&"]) return s;
    NSMutableString *r = [s mutableCopy];
    [r replaceOccurrencesOfString:@"&lt;" withString:@"<" options:0 range:NSMakeRange(0, r.length)];
    [r replaceOccurrencesOfString:@"&gt;" withString:@">" options:0 range:NSMakeRange(0, r.length)];
    [r replaceOccurrencesOfString:@"&amp;" withString:@"&" options:0 range:NSMakeRange(0, r.length)];
    [r replaceOccurrencesOfString:@"&quot;" withString:@"\"" options:0 range:NSMakeRange(0, r.length)];
    [r replaceOccurrencesOfString:@"&apos;" withString:@"'" options:0 range:NSMakeRange(0, r.length)];
    return r;
}

#pragma mark - Accessors

- (NSArray<NSString *> *)allLanguageNames {
    NSMutableArray *names = [NSMutableArray new];
    for (NppLangDef *l in _langs) [names addObject:l.name];
    return names;
}

- (NSDictionary<NSString *, NSString *> *)extensionMap {
    return [_extMap copy];
}

- (nullable NppLangDef *)langDefForName:(NSString *)name {
    return _langsByName[name.lowercaseString];
}

- (nullable NSString *)languageForExtension:(NSString *)ext {
    return _extMap[ext.lowercaseString];
}

- (nullable NSString *)commentLineForLanguage:(NSString *)lang {
    NppLangDef *def = _langsByName[lang.lowercaseString];
    return def.commentLine.length ? def.commentLine : nil;
}

- (nullable NSString *)commentStartForLanguage:(NSString *)lang {
    NppLangDef *def = _langsByName[lang.lowercaseString];
    return def.commentStart.length ? def.commentStart : nil;
}

- (nullable NSString *)commentEndForLanguage:(NSString *)lang {
    NppLangDef *def = _langsByName[lang.lowercaseString];
    return def.commentEnd.length ? def.commentEnd : nil;
}

- (nullable NSString *)keywordsForLanguage:(NSString *)lang keywordClass:(NSString *)kwClass {
    NppLangDef *def = _langsByName[lang.lowercaseString];
    return def.keywords[kwClass];
}

@end
