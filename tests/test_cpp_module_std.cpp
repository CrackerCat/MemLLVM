import std;

int main() {
    std::vector<int> v;
    v.push_back(10);
    v.push_back(20);
    v.push_back(30);

    int sum = 0;
    for (int x : v) {
        sum += x;
    }

    if (sum == 60) {
        return 77;
    }
    return 1;
}
