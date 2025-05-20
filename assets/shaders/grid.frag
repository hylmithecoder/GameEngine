#version 330 core
out vec4 FragColor;
    
uniform vec2 uViewport;    // Viewport size
uniform vec2 uPan;         // Pan offset
uniform float uZoom;       // Zoom level
uniform vec3 uGridColor;   // Grid line color
uniform vec3 uBgColor;     // Background color
uniform float uGridSize;   // Grid cell size
    
void main() {
    // Ukuran grid dalam pixel (dipengaruhi zoom)
    float gridSize = uGridSize * uZoom;
        
    // Hitung koordinat yang sudah dipan dan di-zoom
    vec2 coord = (gl_FragCoord.xy - uViewport * 0.5) / uZoom + uPan;
        
    // Hitung garis horizontal & vertikal
    float line = step(0.98, abs(fract(coord.x / uGridSize) - 0.5) * 2.0) +
                step(0.98, abs(fract(coord.y / uGridSize) - 0.5) * 2.0);
                    
    // Garis axis X dan Y lebih tebal
    float axis = 0.0;
    if (abs(coord.x) < 1.0 || abs(coord.y) < 1.0) {
        axis = 0.6;
    }
                    
    // Warna final - campuran background dan grid
    vec3 finalColor = mix(uBgColor, uGridColor, max(line, axis));
        
    FragColor = vec4(finalColor, 1.0);
}