#include <vector>
#include <iostream>
#include <unordered_map>

int main(){
	std::unordered_map<int, int> v;
    v[1] = 100;

    std::cout << v[1] << "\n";
}