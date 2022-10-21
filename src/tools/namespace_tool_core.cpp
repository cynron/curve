/*
 *  Copyright (c) 2020 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * Project: curve
 * Created Date: 2019-09-25
 * Author: charisu
 */
#include "src/tools/namespace_tool_core.h"

DEFINE_string(userName, "", "owner of the file");
DEFINE_string(password, "", "password of administrator");

namespace curve {
namespace tool {

NameSpaceToolCore::NameSpaceToolCore(std::shared_ptr<MDSClient> client) :
                                            client_(client) {
    client_->SetUserName(FLAGS_userName);
    client_->SetPassword(FLAGS_password);
}

int NameSpaceToolCore::Init(const std::string& mdsAddr) {
    return client_->Init(mdsAddr);
}

int NameSpaceToolCore::GetFileInfo(const std::string &fileName,
                                   FileInfo* fileInfo) {
    return client_->GetFileInfo(fileName, fileInfo);
}

int NameSpaceToolCore::ListDir(const std::string& dirName,
                               std::vector<FileInfo>* files) {
    return client_->ListDir(dirName, files);
}

int NameSpaceToolCore::GetChunkServerListInCopySet(
                                    const PoolIdType& logicalPoolId,
                                    const CopySetIdType& copysetId,
                                    std::vector<ChunkServerLocation>* csLocs) {
    return client_->GetChunkServerListInCopySet(logicalPoolId,
                                                copysetId, csLocs);
}

int NameSpaceToolCore::DeleteFile(const std::string& fileName,
                                  bool forcedelete) {
    return client_->DeleteFile(fileName, forcedelete);
}

int NameSpaceToolCore::CreateFile(const std::string& fileName,
                                  uint64_t length) {
    return client_->CreateFile(fileName, length);
}
int NameSpaceToolCore::ExtendVolume(const std::string& fileName,
                                     uint64_t newSize) {
    return client_->ExtendVolume(fileName, newSize);
}
int NameSpaceToolCore::GetAllocatedSize(const std::string& fileName,
                                        uint64_t* allocSize,
                                        AllocMap* allocMap) {
    return client_->GetAllocatedSize(fileName, allocSize, allocMap);
}

int NameSpaceToolCore::GetFileSize(const std::string& fileName,
                                   uint64_t* fileSize) {
    int ret = client_->GetFileSize(fileName, fileSize);
    if (ret != 0) {
        std::cout << "GetFileSize fail!" << std::endl;
        return -1;
    }
    return 0;
}

int NameSpaceToolCore::GetFileSegments(const std::string& fileName,
                                  std::vector<PageFileSegment>* segments) {
    FileInfo fileInfo;
    int res = GetFileInfo(fileName, &fileInfo);
    if (res != 0) {
        std::cout << "GetFileInfo fail!" << std::endl;
        return -1;
    }
    return GetFileSegments(fileName, fileInfo, segments);
}

int NameSpaceToolCore::GetFileSegments(const std::string& fileName,
                                  const FileInfo& fileInfo,
                                  std::vector<PageFileSegment>* segments) {
    // 只能获取page file的segment
    if (fileInfo.filetype() != curve::mds::FileType::INODE_PAGEFILE) {
        std::cout << "It is not a page file!" << std::endl;
        return -1;
    }

    // 获取文件的segment数，并打印每个segment的详细信息
    uint64_t segmentNum = fileInfo.length() / fileInfo.segmentsize();
    uint64_t segmentSize = fileInfo.segmentsize();
    for (uint64_t i = 0; i < segmentNum; i++) {
        // load  segment
        PageFileSegment segment;
        GetSegmentRes res = client_->GetSegmentInfo(fileName,
                                    i * segmentSize, &segment);
        if (res == GetSegmentRes::kOK) {
            segments->emplace_back(segment);
        } else if (res == GetSegmentRes::kSegmentNotAllocated) {
            continue;
        } else if (res == GetSegmentRes::kFileNotExists) {
            // 查询过程中文件被删掉了，清空segment并返回0
            segments->clear();
            return 0;
        } else {
            std::cout << "Get segment info from mds fail!" << std::endl;
            return -1;
        }
    }
    return 0;
}

int NameSpaceToolCore::CleanRecycleBin(const std::string& dirName) {
    std::vector<FileInfo> files;
    int res = client_->ListDir(curve::mds::RECYCLEBINDIR, &files);
    if (res != 0) {
        std::cout << "List RecycleBin fail!" << std::endl;
        return -1;
    }
    bool success = true;
    for (const auto& fileInfo : files) {
        std::string fileName = curve::mds::RECYCLEBINDIR + "/"
                                + fileInfo.filename();
        // 如果指定了dirName，就只删除原来在这个目录下的文件
        if (!dirName.empty()) {
            std::string originPath = fileInfo.originalfullpathname();
            if (originPath.find(dirName) != 0) {
                continue;
            }
        }
        res = client_->DeleteFile(fileName, true);
        if (res != 0) {
            std::cout << "DeleteFile " << fileName << " fail!" << std::endl;
            success = false;
        }
    }
    if (success) {
        std::cout << "CleanRecycleBin success!" << std::endl;
        return 0;
    }
    return -1;
}

int NameSpaceToolCore::QueryChunkCopyset(const std::string& fileName,
                                     uint64_t offset,
                                     uint64_t* chunkId,
                                     std::pair<uint32_t, uint32_t>* copyset) {
    if (!chunkId || !copyset) {
        std::cout << "The argument is a null pointer!" << std::endl;
        return -1;
    }
    FileInfo fileInfo;
    int res = client_->GetFileInfo(fileName, &fileInfo);
    if (res != 0) {
        std::cout << "Get file info failed!" << std::endl;
        return -1;
    }
    if (fileInfo.filetype() != curve::mds::FileType::INODE_PAGEFILE) {
        std::cout << "It is not a page file!" << std::endl;
        return -1;
    }
    uint64_t segmentSize = fileInfo.segmentsize();
    // segment对齐的offset
    uint64_t segOffset = (offset / segmentSize) * segmentSize;
    PageFileSegment segment;
    GetSegmentRes segRes = client_->GetSegmentInfo(fileName,
                                                segOffset, &segment);
    if (segRes != GetSegmentRes::kOK) {
        if (segRes == GetSegmentRes::kSegmentNotAllocated) {
            std::cout << "Chunk has not been allocated!" << std::endl;
            return -1;
        } else {
            std::cout << "Get segment info from mds fail!" << std::endl;
            return -1;
        }
    }
    // 在segment里面的chunk的索引
    if (segment.chunksize() == 0) {
        std::cout << "No chunks in segment!" << std::endl;
        return -1;
    }
    uint64_t chunkIndex = (offset - segOffset) / segment.chunksize();
    if (chunkIndex >= segment.chunks_size()) {
        std::cout << "ChunkIndex exceed chunks num in segment!" << std::endl;
        return -1;
    }
    PageFileChunkInfo chunk = segment.chunks(chunkIndex);
    *chunkId = chunk.chunkid();
    uint32_t logicPoolId = segment.logicalpoolid();
    uint32_t copysetId = chunk.copysetid();
    *copyset = std::make_pair(logicPoolId, copysetId);
    return 0;
}
}  // namespace tool
}  // namespace curve
