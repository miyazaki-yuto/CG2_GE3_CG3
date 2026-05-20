#pragma once
#include <cmath>

struct Vector3 {
	float x, y, z;

	// コンストラクタ
	Vector3() : x(0), y(0), z(0) {}

	// 値を指定出来るコンストラクタ
	Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

	//--operatorオーバーロード--//

	// 足し算を出来るようにする
	inline Vector3 operator+(const Vector3& other) const
	{
		return Vector3(x + other.x, y + other.y, z + other.z);
	}

	// 引き算を出来るようにする
	inline Vector3 operator-(const Vector3& other) const
	{
		return Vector3(x - other.x, y - other.y, z - other.z); 
	}

	// 掛け算を出来るようにする
	inline Vector3 operator*(const Vector3& other) const
	{
		return Vector3(x * other.x, y * other.y, z * other.z);
	}

	// スカラー倍
	inline Vector3 operator*(float scalar) const
	{
		return Vector3(x * scalar, y * scalar, z * scalar);
	}

	// 割り算を出来るようにする
	inline Vector3 operator/(const Vector3& other) const
	{
		return Vector3(x / other.x, y / other.y, z / other.z);
	}

	// スカラー除算
	inline Vector3 operator/(float scalar) const
	{
		return Vector3(x / scalar, y / scalar, z / scalar);
	}

	// ベクトルの長さ計算
	inline float Length() const
	{
		return std::sqrt(x * x + y * y + z * z);
	}

	// 正規化
	inline void Normalize()
	{
		float len = Length();
		if (len > 0)
		{
			x /= len;
			y /= len;
			z /= len;
		}
	}

	//--静的(static)関数--//

	// 内積
	inline static float Dot(const Vector3& v1, const Vector3& v2)
	{
		return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z; 
	}

	// 2点間の距離計算
	inline static float Distance(const Vector3& v1, const Vector3& v2)
	{
		return (v1 - v2).Length();
	}
};

// float * Vector3 の順番でスカラー倍を計算するためのあれ
inline Vector3 operator*(float scalar, const Vector3& v) 
{
	return Vector3(scalar * v.x, scalar * v.y, scalar * v.z); 
}

// 一旦ココ
// クロス積
inline Vector3 Cross(const Vector3& v1, const Vector3& v2) {
	return Vector3(
		v1.y * v2.z - v1.z * v2.y,
		v1.z * v2.x - v1.x * v2.z,
		v1.x * v2.y - v1.y * v2.x); 
}