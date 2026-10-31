struct VS_IN {
    float3 pos : POSITION;
    float4 col : COLOR;
};
struct PS_IN {
    float4 pos : SV_POSITION;
    float4 col : COLOR;
};
cbuffer ConstantBuffer : register(b0) {
    matrix transform;
};
PS_IN vs_main(VS_IN input) {
    PS_IN output;
    output.pos = mul(float4(input.pos, 1.0f), transform);
    output.col = input.col;
    return output;
}
float4 ps_main(PS_IN input) : SV_TARGET {
    return input.col;
}
