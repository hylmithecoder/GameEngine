__kernel void apply_gravity(__global float4* positions, 
                            __global float4* velocities, 
                            const float4 gravity, 
                            const float deltaTime, 
                            const int numObjects) 
{
    int gid = get_global_id(0);
    if (gid >= numObjects) return;

    // Update kecepatan & posisi di GPU secara paralel
    velocities[gid] += gravity * deltaTime;
    positions[gid] += velocities[gid] * deltaTime;
}
