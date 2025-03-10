#include "BlurFilter.h"

BlurFilter::BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
{
	md3dDevice = device;
	mFormat = format;
	mWidth = width;
	mHeight = height;

	BuildResources();
}

ID3D12Resource* BlurFilter::Resource() const
{
	return mBlurMap0.Get();
}

void BlurFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv, 
	UINT descriptorSize)
{
	mBlur0CpuSrv = hCpuSrv;
	mBlur0CpuUav = hCpuSrv.Offset(1, descriptorSize);
	mBlur1CpuSrv = hCpuSrv.Offset(1, descriptorSize);
	mBlur1CpuUav = hCpuSrv.Offset(1, descriptorSize);

	mBlur0GpuSrv = hGpuSrv;
	mBlur0GpuUav = hGpuSrv.Offset(1, descriptorSize);
	mBlur1GpuSrv = hGpuSrv.Offset(1, descriptorSize);
	mBlur1GpuUav = hGpuSrv.Offset(1, descriptorSize);

	BuildDescriptors();
}

void BlurFilter::OnResize(UINT newWidth, UINT newHeight)
{
	if (mWidth != newWidth || mHeight != newHeight)
	{
		mWidth = newWidth;
		mHeight = newHeight;

		BuildResources();
		BuildDescriptors();
	}
}

void BlurFilter::Execute(ID3D12GraphicsCommandList* cmdList, 
	ID3D12RootSignature* rootSig, 
	ID3D12PipelineState* horzBlurPso, 
	ID3D12PipelineState* vertBlurPso, 
	ID3D12Resource* input, int blurCount)
{
	auto weights = CalcGuassWeights(1.0f);
	int blurRadius = (int)weights.size() / 2;

	cmdList->SetComputeRootSignature(rootSig);
	cmdList->SetComputeRoot32BitConstant(0, blurRadius, 0);
	cmdList->SetComputeRoot32BitConstants(0, 11, weights.data(), 1);

	// 读取后台缓冲区的数据.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(input,
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE));
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

	cmdList->CopyResource(mBlurMap0.Get(), input);

	// 为模糊做准备.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
		D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

	for (int i = 0; i < blurCount; ++i)
	{
		///
		/// 横向模糊.
		///
		cmdList->SetPipelineState(horzBlurPso);

		cmdList->SetComputeRootDescriptorTable(1, mBlur0GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, mBlur1GpuUav);

		UINT numGroupX = (UINT)ceilf(mWidth / 256.0f);
		cmdList->Dispatch(numGroupX, mHeight, 1);

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));

		///
		/// 纵向模糊.
		///
		cmdList->SetPipelineState(vertBlurPso);

		cmdList->SetComputeRootDescriptorTable(1, mBlur1GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, mBlur0GpuUav);

		UINT numGroupY = (UINT)ceilf(mHeight / 256.0f);
		cmdList->Dispatch(mWidth, numGroupY, 1);

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
	}

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COMMON));
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON));
}

std::vector<float> BlurFilter::CalcGuassWeights(float sigma) const
{
	float twoSigma2 = 2 * sigma * sigma;
	int blurRadius = (int)ceilf(2 * sigma);
	assert(blurRadius <= MaxBlurRadius);

	std::vector<float> weights(2 * blurRadius + 1);
	float weightsSum = 0.0f;

	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = (float)i;
		weights[i + blurRadius] = expf(-x * x / twoSigma2);
		weightsSum += weights[i + blurRadius];
	}

	for (int i = 0; i < (int)weights.size(); ++i)
	{
		weights[i] /= weightsSum;
	}

	return weights;
}

void BlurFilter::BuildDescriptors()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = mFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateShaderResourceView(mBlurMap0.Get(), &srvDesc, mBlur0CpuSrv);
	md3dDevice->CreateUnorderedAccessView(mBlurMap0.Get(), nullptr, &uavDesc, mBlur0CpuUav);

	md3dDevice->CreateShaderResourceView(mBlurMap1.Get(), &srvDesc, mBlur1CpuSrv);
	md3dDevice->CreateUnorderedAccessView(mBlurMap1.Get(), nullptr, &uavDesc, mBlur1CpuUav);
}

void BlurFilter::BuildResources()
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Alignment = 0;
	texDesc.DepthOrArraySize = 1;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Format = mFormat;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(mBlurMap0.ReleaseAndGetAddressOf())));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(mBlurMap1.ReleaseAndGetAddressOf())));
}
