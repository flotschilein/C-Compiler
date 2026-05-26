struct Point {
    int x;
    int y;
};

int test_struct_init() {
    struct Point p = {10, 20};
    return p.x + p.y;
}

int test_struct_assign() {
    struct Point a = {1, 2};
    struct Point b = {3, 4};
    a = b;
    return a.x + a.y;
}

int test_compound_literal() {
    struct Point p = (struct Point){5, 6};
    return p.x + p.y;
}

int test_array_init() {
    int arr[3] = {10, 20, 30};
    return arr[0] + arr[1] + arr[2];
}
