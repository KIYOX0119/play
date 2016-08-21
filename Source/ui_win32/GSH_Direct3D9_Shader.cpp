#include <D3Dcompiler.h>
#include "GSH_Direct3D9.h"
#include "nuanceur/generators/HlslShaderGenerator.h"

CGSH_Direct3D9::VertexShaderPtr CGSH_Direct3D9::CreateVertexShader(SHADERCAPS caps)
{
	HRESULT result = S_OK;
	auto shaderCode = GenerateVertexShader(caps);

	auto shaderSource = Nuanceur::CHlslShaderGenerator::Generate("main", shaderCode);

	UINT compileFlags = 0;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG;
#endif

	Framework::Win32::CComPtr<ID3DBlob> shaderBinary;
	Framework::Win32::CComPtr<ID3DBlob> compileErrors;
	result = D3DCompile(shaderSource.c_str(), shaderSource.length() + 1, "vs", nullptr, nullptr, "main", 
		"vs_3_0", compileFlags, 0, &shaderBinary, &compileErrors);
	assert(SUCCEEDED(result));

	VertexShaderPtr shader;
	result = m_device->CreateVertexShader(reinterpret_cast<DWORD*>(shaderBinary->GetBufferPointer()), &shader);
	assert(SUCCEEDED(result));

	return shader;
}

CGSH_Direct3D9::PixelShaderPtr CGSH_Direct3D9::CreatePixelShader(SHADERCAPS caps)
{
	HRESULT result = S_OK;
	auto shaderCode = GeneratePixelShader(caps);

	auto shaderSource = Nuanceur::CHlslShaderGenerator::Generate("main", shaderCode, 
		Nuanceur::CHlslShaderGenerator::FLAG_COMBINED_SAMPLER_TEXTURE);

	UINT compileFlags = 0;
#ifdef _DEBUG
	compileFlags |= D3DCOMPILE_DEBUG;
#endif

	Framework::Win32::CComPtr<ID3DBlob> shaderBinary;
	Framework::Win32::CComPtr<ID3DBlob> compileErrors;
	result = D3DCompile(shaderSource.c_str(), shaderSource.length() + 1, "ps", nullptr, nullptr, "main", 
		"ps_3_0", compileFlags, 0, &shaderBinary, &compileErrors);
	assert(SUCCEEDED(result));

	PixelShaderPtr shader;
	result = m_device->CreatePixelShader(reinterpret_cast<DWORD*>(shaderBinary->GetBufferPointer()), &shader);
	assert(SUCCEEDED(result));

	return shader;
}

Nuanceur::CShaderBuilder CGSH_Direct3D9::GenerateVertexShader(SHADERCAPS caps)
{
	using namespace Nuanceur;

	auto b = CShaderBuilder();

	{
		//Inputs
		auto inputPosition = CFloat4Lvalue(b.CreateInput(SEMANTIC_POSITION));
		auto inputTexCoord = CFloat4Lvalue(b.CreateInput(SEMANTIC_TEXCOORD, 0));
		auto inputColor = CFloat4Lvalue(b.CreateInput(SEMANTIC_TEXCOORD, 1));

		//Outputs
		auto outputPosition = CFloat4Lvalue(b.CreateOutput(SEMANTIC_SYSTEM_POSITION));
		auto outputTexCoord = CFloat4Lvalue(b.CreateOutput(SEMANTIC_TEXCOORD, 0));
		auto outputColor = CFloat4Lvalue(b.CreateOutput(SEMANTIC_TEXCOORD, 1));

		//Constants
		auto projMatrix = CMatrix44Value(b.CreateUniformMatrix("g_projMatrix"));

		outputPosition = projMatrix * NewFloat4(inputPosition->xyz(), 1.0f);
		outputTexCoord = inputTexCoord->xyzw();
		outputColor = inputColor->xyzw();
	}

	return b;
}

Nuanceur::CShaderBuilder CGSH_Direct3D9::GeneratePixelShader(SHADERCAPS caps)
{
	using namespace Nuanceur;

	auto b = CShaderBuilder();

	{
		//Inputs
		auto inputTexCoord = CFloat4Lvalue(b.CreateInput(SEMANTIC_TEXCOORD, 0));
		auto inputColor = CFloat4Lvalue(b.CreateInput(SEMANTIC_TEXCOORD, 1));

		//Outputs
		auto outputColor = CFloat4Lvalue(b.CreateOutput(SEMANTIC_SYSTEM_COLOR));

		//Textures
		auto texture = CTexture2DValue(b.CreateTexture2D(0));

		//Temporaries
		auto textureColor = CFloat4Lvalue(b.CreateTemporary());

		if(caps.hasTexture)
		{
			textureColor = Sample(texture, inputTexCoord->xy());
		}
		else
		{
			textureColor = NewFloat4(b, 1, 1, 1, 1);
		}

		outputColor = inputColor * textureColor;
	}

	return b;
}