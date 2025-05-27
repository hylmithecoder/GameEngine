#include <iostream>
using namespace std;

class Train 
{
    public:

    string name;

    void printName()
    {
        cout << name << " a Train CPP Class";
    }
};

// Its Like a public static void Main(string[] args) in C# and Java
int main()
{
    Train classTrain;
    classTrain.name = "Hylmi";
    classTrain.printName();

    // Return 0 to Operating System
    return 0;
}