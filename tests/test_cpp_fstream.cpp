#include <fstream>
#include <string>

int main() {
    {
        std::ofstream ofs("test_fstream.txt");
        ofs << "MemLLVM_Fstream_Success\n";
        ofs.close();
    }

    std::string line;
    {
        std::ifstream ifs("test_fstream.txt");
        ifs >> line;
        ifs.close();
    }

    if (line == "MemLLVM_Fstream_Success") {
        return 42; // Magic verification exit code
    }
    return 1;
}
