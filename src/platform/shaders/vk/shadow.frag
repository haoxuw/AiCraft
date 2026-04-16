#version 450

// Depth-only pass — nothing to output. Present for driver portability:
// some drivers/validation layers require a fragment shader even with
// no color attachments.

void main() { }
