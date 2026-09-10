#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <algorithm>
#include <iostream>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;
    RawMemory(const RawMemory&) = delete;
    RawMemory& operator=(const RawMemory& rhs) = delete;
    RawMemory(RawMemory&& other) noexcept { 
        Swap(other);
    }   
    RawMemory& operator=(RawMemory&& rhs) noexcept { 
        if(this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }   

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
}; 

template <typename T>
class Vector {
public:
    
    // Итераторы 
    using iterator = T*;
    using const_iterator = const T*;
    
    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return data_ + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return data_ + size_;
    }
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator cend() const noexcept {
        return data_ + size_;
    }

    Vector() noexcept = default;

    Vector(Vector&& other) noexcept 
    : size_(std::exchange(other.size_, 0)) {
        data_.Swap(other.data_);
    }

    Vector& operator=(const Vector& rhs) {
        if (this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector rhs_copy(rhs);
                Swap(rhs_copy);
            } else {
                InitSwap(rhs);
            }
        }
        return *this;
    }
    Vector& operator=(Vector&& rhs) noexcept {
        if(this != &rhs) {
            data_.Swap(rhs.data_);
            std::swap(size_, rhs.size_);
        }
        
        return *this;
    } 

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    // Создание пустого вектора 
    explicit Vector(size_t size)
        : data_(size)
        , size_(size)  
    {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }
    // Создание вектора из другого вектора 
    Vector(const Vector& other)
        : data_(other.size_)
        , size_(other.size_)  
    {
        std::uninitialized_copy_n(other.data_.GetAddress(), size_, data_.GetAddress());
    }

    ~Vector() {
        DestroyN(data_.GetAddress(), size_);
    }

    void Resize(size_t new_size) {
        if(new_size <= 0 || new_size == size_) {
            return;
        }

        if(new_size > size_) {
            // Пытаемся выделить новую память
            Reserve(new_size);
            // В случае успеха заполняем "нулями" выделенную память
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        else {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
        }
        // Меняем размер вектора 
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        if(size_ < data_.Capacity()) {
            new (data_ + size_) T(std::forward<Args>(args)...);
            ++size_;
            return *(data_ + size_ - 1);
        } else {
            ReallocateAndInsertBack(std::forward<Args>(args)...);
            return *(data_ + size_ - 1);
        }
    }
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if(pos == data_ + size_) {
            return &EmplaceBack(std::forward<Args>(args)...);
        }
        // Проверка на соблюдение итератором pos границ вектора 
        assert(pos >= data_.GetAddress() && pos <= data_ + size_);

        size_t offset = pos - data_.GetAddress();
        if(size_ < data_.Capacity()) {
            T temp(std::forward<Args>(args)...);
            if constexpr(std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
                // Сначала перемещаем последний элемент на позицию вправо 
                std::uninitialized_move_n(data_ + size_ - 1, 1, data_ + size_);
                // Затем перемещаем элементы идущие за вставляемы на один вправо 
                std::move_backward(data_ + offset, data_ + size_ - 1, data_ + size_);
            } else {
                std::uninitialized_copy_n(data_ + size_ - 1, 1, data_ + size_);
                
                std::copy_backward(data_ + offset, data_ + size_ - 1, data_ + size_);
            }
            data_[offset] = std::move(temp);
            ++size_;
            return data_ + offset;

        } else {
            ReallocateAndInsert(pos, std::forward<Args>(args)...);
            return data_ + offset;
        }
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        // Проверка на соблюдение итератором pos границ вектора 
        assert(pos >= data_.GetAddress() && pos < data_ + size_);

        size_t offset = pos - data_.GetAddress();
        if constexpr(std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::move(data_ + offset + 1, data_ + size_, data_ + offset);
        } else {
            std::copy(data_ + offset + 1, data_ + size_, data_ + offset);
        }
        Destroy(data_ + size_ - 1);
        --size_;

        return data_ + offset;

    }

    void PopBack() noexcept {
        if(size_ == 0) {
            return;
        }
        Destroy(data_ + (size_ - 1));
        --size_;
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= data_.Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        if constexpr (std::is_nothrow_move_constructible_v<T> || 
            !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        else {
            std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());
        }
        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
    }

private:
    // Реаллокация с вставкой в конец вектора 
    template <typename... Args>
    void ReallocateAndInsertBack(Args&&... args) {
        size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
        // Выделяем новый участок памяти
        RawMemory<T> new_data(new_capacity);
        // Сохраняем копию нового элемента, чтобы избежать добавления в вектор moved-from 
        new(new_data.GetAddress() + size_) T(std::forward<Args>(args)...);
        
        try {
            if constexpr(std::is_nothrow_move_constructible_v<T>) {
            std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());

            }
            else if constexpr(std::is_copy_constructible_v<T>) {
                std::uninitialized_copy_n(data_.GetAddress(), size_, new_data.GetAddress());

            }
            else {
                std::uninitialized_move_n(data_.GetAddress(), size_, new_data.GetAddress());
            } 
        } catch (...) {
            Destroy(new_data.GetAddress() + size_);
        }
        
        DestroyN(data_.GetAddress(), size_);
        data_.Swap(new_data);
        ++size_;
    }
    // Реаллокация с вставкой в произвольное место
    template <typename... Args>
    void ReallocateAndInsert(const_iterator pos, Args&&... args) {
        
        size_t new_capacity = size_ == 0 ? 1 : size_ * 2;
        size_t offset = pos - data_.GetAddress();

        RawMemory<T> new_data(new_capacity);

        new(new_data + offset) T(std::forward<Args>(args)...);

        if constexpr(std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            try {
                // Сначала перемещаем элементы которые предшествуют вставляемому 
                std::uninitialized_move_n(data_.GetAddress(), offset, new_data.GetAddress());
            } catch (...) {
                Destroy(new_data + offset);
                throw;
            }
            try {
                // Затем перемещаем элементы идущие за вставляемым
                std::uninitialized_move_n(data_ + offset, size_ - offset, new_data + offset + 1);
            } catch (...) {
                DestroyN(new_data.GetAddress(), offset);
                throw;
            }
        }
        else {
            try {
                // Сначала копируем элементы которые предшествуют вставляемому 
                std::uninitialized_copy_n(data_.GetAddress(), offset, new_data.GetAddress());
            } catch (...) {
                Destroy(new_data + offset);
                throw;
            }
            try {
                // Затем перемещаем элементы идущие за вставляемым
                std::uninitialized_copy_n(data_ + offset, size_ - offset, new_data + offset + 1);
            } catch (...) {
                DestroyN(new_data.GetAddress(), offset);
                throw;
            }
        }
        DestroyN(data_.GetAddress(), size_);
        data_.Swap(new_data);
        ++size_;
    }

    void InitSwap(const Vector& rhs) {
        const size_t common = std::min(size_, rhs.size_);
        std::copy_n(rhs.data_.GetAddress(), common, data_.GetAddress());
        if (size_ < rhs.size_) {
        // uninitialized_copy_n создаёт объекты "с нуля" в сырой памяти
        std::uninitialized_copy_n(
            rhs.data_ + size_,          // Откуда копируем (начало новых элементов в rhs)
            rhs.size_ - size_,          // Сколько копируем
            data_ + size_               // Куда копируем (свободное место в нашем векторе)
            );
        } 
        // 3. Если наш вектор был больше -> нужно удалить лишние объекты
        else if (size_ > rhs.size_) {
            // destroy_n вызывает деструкторы для лишних объектов, освобождая ресурсы внутри них
            std::destroy_n(data_ + rhs.size_, size_ - rhs.size_);
        }

        // 4. Обновляем размер
        size_ = rhs.size_;
    }

    // Вызывает деструкторы n объектов массива по адресу buf
    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }

    void PrintBuffer() {
        for(size_t i = 0; i < size_; ++i) {
            std::cout << "adress: " << data_.GetAddress() + i << data_[i];
        }
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};
