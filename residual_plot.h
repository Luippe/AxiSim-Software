#pragma once
#include <string>
#include <mutex>


class ResidualPlot {
public:
	ResidualPlot(std::initializer_list<std::string> residualNames);

	std::vector<int> iterations;
	std::vector<std::string> names;
	std::vector<float> values;

    std::mutex mutex; // use lock to prevent solver thread from colliding with main thread

    // draw plot
	void draw();

    // clear all residuals
    void clear();

    // template for adding residuals
    template<typename... Args>
    void add(int iter, Args... residuals) {
        constexpr size_t count = sizeof...(residuals);

        if (count != names.size()) {
            throw std::runtime_error("Residual count does not match residual names.");
        }

        std::lock_guard<std::mutex> lock(mutex);

        iterations.push_back(iter);

        float vals[] = {
            static_cast<float>(residuals)...
        };

        for (size_t i = 0; i < count; ++i) {
            values.push_back(vals[i]);
        }
    }
};