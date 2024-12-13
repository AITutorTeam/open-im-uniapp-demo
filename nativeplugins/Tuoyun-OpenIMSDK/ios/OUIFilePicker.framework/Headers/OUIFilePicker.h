//
//  OUIFilePicker.h
//  OUIFilePicker
//
//  Created by x on 2023/4/4.
//

#import "DCUniModule.h"

NS_ASSUME_NONNULL_BEGIN

@interface OUIFilePicker : DCUniModule

/**
 选择文件
 @param callback 回调选择的文件的本机路径
 */
- (void)pick:(UniModuleKeepAliveCallback)callback;

/**
 预览文件
 @param path 文件的本机路径
 */
- (void)preview:(NSString *)path;

/**
 视频缩略图
 @param path 文件的本机路径
 */
- (void)getVideoAttributes:(NSString *)path callback:(UniModuleKeepAliveCallback)callback;
@end

NS_ASSUME_NONNULL_END
