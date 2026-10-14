#pragma once
#include "Cafe/OS/RPL/COSModule.h"

namespace nn::nfp
{
	uint32 NFCGetTagInfo(uint32 index, uint32 timeout, MPTR functionPtr, void* userParam);

	COSModule* GetModule();
}

void nnNfp_load();
void nnNfp_update();

bool nnNfp_isInitialized();
bool nnNfp_touchNfcTagFromFile(const fs::path& filePath, uint32* nfcError);
// PC Edition: the title is looking for a tag (StartDetection .. StopDetection) and none is on the reader
bool nnNfp_isWaitingForTag();
std::string nnNfp_debugState();

#define NFP_STATE_NONE			(0)
#define NFP_STATE_INIT			(1)
#define NFP_STATE_RW_SEARCH		(2)
#define NFP_STATE_RW_ACTIVE		(3)
#define NFP_STATE_RW_DEACTIVE	(4)
#define NFP_STATE_RW_MOUNT		(5)
#define NFP_STATE_UNEXPECTED	(6)
#define NFP_STATE_RW_MOUNT_ROM	(7)
