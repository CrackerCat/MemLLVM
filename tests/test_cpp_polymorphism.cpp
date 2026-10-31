#include <windows.h>

class Shape {
public:
    virtual ~Shape() {}
    virtual int area() = 0;
};

class Box : public Shape {
private:
    int w, h;
public:
    Box(int width, int height) : w(width), h(height) {}
    ~Box() override {}
    int area() override {
        return w * h;
    }
};

class Circle : public Shape {
private:
    int r;
public:
    Circle(int radius) : r(radius) {}
    ~Circle() override {}
    int area() override {
        return 3 * r * r;
    }
};

__declspec(noinline) int calculateTotalArea(Shape* s1, Shape* s2) {
    return s1->area() + s2->area();
}

int main() {
    Box box(10, 20); // area = 200
    Circle circ(5);  // area = 3 * 25 = 75

    Shape* s1 = &box;
    Shape* s2 = &circ;

    return calculateTotalArea(s1, s2); // Expected: 275
}
