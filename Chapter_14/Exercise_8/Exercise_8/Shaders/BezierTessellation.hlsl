// 基础光源的定义.
#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 0
#endif

#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

/// ObjectCB的常量缓冲区结构体.
struct ObjectConstants
{
	float4x4 gWorld;
	float4x4 gTexTransform;
};

/// MaterialCB的常量缓冲区结构体.
struct MaterialConstants
{
	float4 gDiffsueAlbedo;
	float3 gFresnelR0;
	float gRoughness;
	float4x4 gMatTransform;
};

/// PassCB 的常量缓冲区结构体.
struct PassConstants
{
	float4x4 gView;
	float4x4 gInvView;
	float4x4 gProj;
	float4x4 gInvProj;
	float4x4 gViewProj;
	float4x4 gInvViewProj;

	float3 gEyePosW;
	float cbPad0;
	float2 gRenderTargetSize;
	float2 gInvRenderTargetSize;

	float gNearZ;
	float gFarZ;
	float gTotalTime;
	float gDeltaTime;

	float4 gAmbientLight;
	float4 gFogColor;
	float gFogStart;
	float gFogRange;
	float2 cbPad1;
	Light gLights[MaxLights];
};


/// 常量缓冲区定义.
ConstantBuffer<ObjectConstants> gObjectConstants 	 : register(b0);
ConstantBuffer<MaterialConstants> gMaterialConstants : register(b1);
ConstantBuffer<PassConstants> gPassConstants 		 : register(b2);

/// 着色器输入纹理资源.
Texture2D gDiffuseMap : register(t0);

/// 静态采样器.
SamplerState gsamPointWrap        : register(s0);
SamplerState gsamPointClamp       : register(s1);
SamplerState gsamLinearWrap       : register(s2);
SamplerState gsamLinearClamp      : register(s3);
SamplerState gsamAnisotropicWrap  : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

///
/// 顶点着色器阶段.
///

/// 顶点着色器输入.
struct VertexIn
{
	float3 PosL : POSITION;
};


/// 顶点着色器输出.
struct VertexOut
{
	float3 PosL : POSITION;
};


/// 顶点着色器, 在这里完全沦陷, 相当于充数但是有不可缺少的步骤.
VertexOut VS(VertexIn vin)
{
	VertexOut vout;
	vout.PosL = vin.PosL;

	return vout;
}
///
/// 外壳着色器阶段.
///

/// 曲面细分因子.
struct PatchTess
{
	float EdgeTess[4]   : SV_TessFactor;
	float InsideTess[2] : SV_InsideTessFactor;
};


/// 常量外壳着色器.
PatchTess ConstantHS(InputPatch<VertexOut, 16> patch, uint patchID : SV_PrimitiveID)
{
	PatchTess pt;

	// 曲面细分因子获取.
	pt.EdgeTess[0] = 25;
	pt.EdgeTess[1] = 25;
	pt.EdgeTess[2] = 25;
	pt.EdgeTess[3] = 25;

	pt.InsideTess[0] = 25;
	pt.InsideTess[1] = 25;

	return pt;
}



/// 外壳着色器输出.
struct HullOut
{
	float3 PosL : POSITION;
};

/// 控制点外壳着色器.
[domain("quad")]
[partitioning("integer")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(16)]
[patchconstantfunc("ConstantHS")]
[maxtessfactor(64.0)]
HullOut HS(InputPatch<VertexOut, 16> p, uint i : SV_OutputControlPointID, uint patchID : SV_PrimitiveID)
{
	HullOut hout;
	hout.PosL = p[i].PosL;

	return hout;
}



///
/// 域着色器阶段.
///

/// 域着色器输出.
struct DomainOut
{
	float4 PosH   : SV_POSITION;
	float3 PosW   : POSITION;
	float3 Normal : NORMAL;
	float2 TexC   : TEXCOORD;
};

/// 计算三次贝塞尔曲线的伯恩斯坦基系数.
float4 BernsteinBasis(float t)
{
	float invT = 1.0 - t;

	return float4(invT * invT * invT,
		3.0 * t * invT * invT,
		3.0 * t * t * invT,
		t * t * t);
}

/// 对伯恩斯坦基系数求导, 以计算控制点的切线.
float4 dBernsteinBasis(float t)
{
	float invT = 1.0 - t;

	return float4(-3 * invT * invT,
		3.0 * invT * invT - 6 * t * invT,
		6.0 * t * invT - 3 * t * t,
		3 * t * t);
}

/// 求贝塞尔曲面上的点.
float3 CubicBezierSum(const OutputPatch<HullOut, 16> bezpatch, float4 basisU, float4 basisV)
{
	float3 sum = float3(0.0f, 0.0f, 0.0f);
	sum = basisV.x * (basisU.x * bezpatch[0].PosL + basisU.y * bezpatch[1].PosL + 
		basisU.z * bezpatch[2].PosL + basisU.w * bezpatch[3].PosL);

	sum += basisV.y * (basisU.x * bezpatch[4].PosL + basisU.y * bezpatch[5].PosL + 
		basisU.z * bezpatch[6].PosL + basisU.w * bezpatch[7].PosL);

	sum += basisV.z * (basisU.x * bezpatch[8].PosL + basisU.y * bezpatch[9].PosL + 
		basisU.z * bezpatch[10].PosL + basisU.w * bezpatch[11].PosL);

	sum += basisV.w * (basisU.x * bezpatch[12].PosL + basisU.y * bezpatch[13].PosL +
		basisU.z * bezpatch[14].PosL + basisU.w * bezpatch[15].PosL);

	return sum;
}

/// 域着色器.
[domain("quad")]
DomainOut DS(PatchTess patchTess, float2 uv : SV_DomainLocation, const OutputPatch<HullOut, 16> bezPatch)
{
	DomainOut dout;

	float4 basisU = BernsteinBasis(uv.x);
	float4 basisV = BernsteinBasis(uv.y);

	// 计算位置.
	float3 p = CubicBezierSum(bezPatch, basisU, basisV);

	// 计算u和v方向的偏导数.
	float4 dBasisU = dBernsteinBasis(uv.x);
	float4 dBasisV = dBernsteinBasis(uv.y);

	// 计算u和v方向的切线.
	float3 dpdu = CubicBezierSum(bezPatch, dBasisU, basisV);
	float3 dpdv = CubicBezierSum(bezPatch, basisU, dBasisV);

	// 叉积计算法线.
	float3 normal = cross(dpdu, dpdv);
	normal = mul(float4(normal, 1.0f), gObjectConstants.gWorld).xyz;

	dout.PosW = mul(float4(p, 1.0f), gObjectConstants.gWorld).xyz;
	dout.PosH = mul(float4(dout.PosW, 1.0), gPassConstants.gViewProj);
	dout.Normal = normal;
	
	float4 texC = mul(float4(uv, 0.0, 1.0), gObjectConstants.gTexTransform);
	dout.TexC = mul(texC, gMaterialConstants.gMatTransform).xy;

	return dout;
}


/// 像素着色器.
float4 PS(DomainOut pin) : SV_Target
{
	// 纹理采样.
	float4 diffuseAlbedo = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC) *
		gMaterialConstants.gDiffsueAlbedo;

	// 获取环境光.
	float4 ambient = gPassConstants.gAmbientLight * diffuseAlbedo;

	// 光照计算必需.
	pin.Normal = normalize(pin.Normal);
	float3 toEyeW = normalize(gPassConstants.gEyePosW - pin.PosW.xyz);

	// 光照计算.
	const float gShininess = 1 - gMaterialConstants.gRoughness;
	Material mat = { diffuseAlbedo, gMaterialConstants.gFresnelR0, gShininess };
	float4 dirLight = ComputeLighting(gPassConstants.gLights, mat, pin.PosW.xyz, pin.Normal, toEyeW, 1.0);

	float4 finalColor = ambient + dirLight;
	finalColor.a = diffuseAlbedo.a;

	return finalColor;
}