#include <glm/glm.hpp>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <windows.h>
using namespace glm;
using namespace std;
class TestGLM
{
    public :
    vec2 position2d = vec2(1.0f, 1.0f);
    vec3 position3d = vec3(1.0f, 1.0f, 1.0f);
    vec4 color = vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mat4 matrix = mat4(1.0f);

    void ShowPosition2d(vec2 currentPosition)
    {
        cout << "Show Position 2d" << endl;
        cout << currentPosition.x << endl;
        cout << currentPosition.y << endl;
    }

    void ShowPosition3d(vec3 currentPosition)
    {
        cout << "Show Position 3d" << endl;
        cout << currentPosition.x << endl;
        cout << currentPosition.y << endl;
        cout << currentPosition.z << endl;
    }

    void ShowColor(vec4 currentColor)
    {
        cout << "Show Color" << endl;
        cout << currentColor.r << endl;
        cout << currentColor.g << endl;
        cout << currentColor.b << endl;
        cout << currentColor.a << endl;
    }

    void ShowMatrix(mat4 currentMatrix)
    {
        cout << "Show Matrix" << endl;
        cout << currentMatrix[0][0] << endl;
        cout << currentMatrix[0][1] << endl;
        cout << currentMatrix[0][2] << endl;
        cout << currentMatrix[0][3] << endl;
        cout << currentMatrix[1][0] << endl;
        cout << currentMatrix[1][1] << endl;
        cout << currentMatrix[1][2] << endl;
        cout << currentMatrix[1][3] << endl;
        cout << currentMatrix[2][0] << endl;
        cout << currentMatrix[2][1] << endl;
        cout << currentMatrix[2][2] << endl;
        cout << currentMatrix[2][3] << endl;
        cout << currentMatrix[3][0] << endl;
        cout << currentMatrix[3][1] << endl;
        cout << currentMatrix[3][2] << endl;
        cout << currentMatrix[3][3] << endl;
    }

};

int main(int argc, char* argv[])
{
    TestGLM test;
    test.ShowPosition2d(test.position2d);
    test.ShowPosition3d(test.position3d);
    test.ShowColor(test.color);
    test.ShowMatrix(test.matrix);

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main(__argc, __argv);
}