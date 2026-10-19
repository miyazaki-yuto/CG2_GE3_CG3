#pragma once
#include "Vector3.h"
#include <cmath>

struct Matrix4x4 {
	float m[4][4];
};

// 行列の加法
inline Matrix4x4 Add(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = m1.m[row][column] + m2.m[row][column];
		}
	}
	return result;
};

// 行列の減法
inline Matrix4x4 Subtract(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = m1.m[row][column] - m2.m[row][column];
		}
	}
	return result;
};

// 行列の積
inline Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = 0.0f;
			for (int k = 0; k < 4; ++k) {
				result.m[row][column] += m1.m[row][k] * m2.m[k][column];
			}
		}
	}
	return result;
};

// 逆行列
inline Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 result;
	float a00 = m.m[0][0], a01 = m.m[0][1], a02 = m.m[0][2], a03 = m.m[0][3];
	float a10 = m.m[1][0], a11 = m.m[1][1], a12 = m.m[1][2], a13 = m.m[1][3];
	float a20 = m.m[2][0], a21 = m.m[2][1], a22 = m.m[2][2], a23 = m.m[2][3];
	float a30 = m.m[3][0], a31 = m.m[3][1], a32 = m.m[3][2], a33 = m.m[3][3];

	float b00 = a00 * a11 - a01 * a10;
	float b01 = a00 * a12 - a02 * a10;
	float b02 = a00 * a13 - a03 * a10;
	float b03 = a01 * a12 - a02 * a11;
	float b04 = a01 * a13 - a03 * a11;
	float b05 = a02 * a13 - a03 * a12;
	float b06 = a20 * a31 - a21 * a30;
	float b07 = a20 * a32 - a22 * a30;
	float b08 = a20 * a33 - a23 * a30;
	float b09 = a21 * a32 - a22 * a31;
	float b10 = a21 * a33 - a23 * a31;
	float b11 = a22 * a33 - a23 * a32;

	float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	if (det == 0.0f) {
		return m; // 逆行列が0の時は返す
	}

	float invDet = 1.0f / det;

	result.m[0][0] = (a11 * b11 - a12 * b10 + a13 * b09) * invDet;
	result.m[0][1] = (a02 * b10 - a01 * b11 - a03 * b09) * invDet;
	result.m[0][2] = (a31 * b05 - a32 * b04 + a33 * b03) * invDet;
	result.m[0][3] = (a22 * b04 - a21 * b05 - a23 * b03) * invDet;

	result.m[1][0] = (a12 * b08 - a10 * b11 - a13 * b07) * invDet;
	result.m[1][1] = (a00 * b11 - a02 * b08 + a03 * b07) * invDet;
	result.m[1][2] = (a32 * b02 - a30 * b05 - a33 * b01) * invDet;
	result.m[1][3] = (a20 * b05 - a22 * b02 + a23 * b01) * invDet;

	result.m[2][0] = (a10 * b10 - a11 * b08 + a13 * b06) * invDet;
	result.m[2][1] = (a01 * b08 - a00 * b10 - a03 * b06) * invDet;
	result.m[2][2] = (a30 * b04 - a31 * b02 + a33 * b00) * invDet;
	result.m[2][3] = (a21 * b02 - a20 * b04 - a23 * b00) * invDet;

	result.m[3][0] = (a11 * b07 - a10 * b09 - a12 * b06) * invDet;
	result.m[3][1] = (a00 * b09 - a01 * b07 + a02 * b06) * invDet;
	result.m[3][2] = (a31 * b01 - a30 * b03 - a32 * b00) * invDet;
	result.m[3][3] = (a20 * b03 - a21 * b01 + a22 * b00) * invDet;

	return result;
};

// 転置行列
inline Matrix4x4 Transpose(const Matrix4x4& m) {
	Matrix4x4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.m[row][column] = m.m[column][row];
		}
	}
	return result;
};

// 単位行列の作成
inline Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			if (row == column) {
				result.m[row][column] = 1.0f;
			} else {
				result.m[row][column] = 0.0f;
			}
		}
	}
	return result;
};

// 平行移動行列
inline Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
};

// 拡大縮小行列
inline Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	return result;
};

// 座標変換
inline Vector3 Transform(const Vector3& vector, const Matrix4x4& matrix) {
	Vector3 result;
	float x = vector.x;
	float y = vector.y;
	float z = vector.z;

	float w = matrix.m[0][3] * x + matrix.m[1][3] * y + matrix.m[2][3] * z + matrix.m[3][3];

	result.x = (x * matrix.m[0][0] + y * matrix.m[1][0] + z * matrix.m[2][0] + 1.0f * matrix.m[3][0]) / w;
	result.y = (x * matrix.m[0][1] + y * matrix.m[1][1] + z * matrix.m[2][1] + 1.0f * matrix.m[3][1]) / w;
	result.z = (x * matrix.m[0][2] + y * matrix.m[1][2] + z * matrix.m[2][2] + 1.0f * matrix.m[3][2]) / w;

	return result;
};

// X軸回転行列
inline Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[1][1] = std::cos(radian);
	result.m[1][2] = std::sin(radian);
	result.m[2][1] = -std::sin(radian);
	result.m[2][2] = std::cos(radian);
	return result;
};

// Y軸回転行列
inline Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = std::cos(radian);
	result.m[0][2] = -std::sin(radian);
	result.m[2][0] = std::sin(radian);
	result.m[2][2] = std::cos(radian);
	return result;
};

// Z軸回転行列
inline Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = std::cos(radian);
	result.m[0][1] = std::sin(radian);
	result.m[1][0] = -std::sin(radian);
	result.m[1][1] = std::cos(radian);
	return result;
};

inline Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3 translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);
	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	// 回転合成
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateXMatrix, rotateYMatrix), rotateZMatrix);
	// アフィン変換行列の合成
	Matrix4x4 result = Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);

	return result;
};

// 透視投影行列
inline Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRation, float rearClip, float farClip) {
	Matrix4x4 result = {0}; // すべて0で初期化

	float cot = 1.0f / std::tan(fovY / 2.0f);

	result.m[0][0] = cot / aspectRation;
	result.m[1][1] = cot;
	result.m[2][2] = farClip / (farClip - rearClip);

	result.m[2][3] = 1.0f;

	result.m[3][2] = (-rearClip * farClip) / (farClip - rearClip);

	return result;
}

// 正射影行列
inline Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = 2.0f / (right - left);
	result.m[1][1] = 2.0f / (top - bottom);
	result.m[2][2] = 1.0f / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	return result;
}

// ビューボード変換行列
inline Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[0][0] = width / 2.0f;
	result.m[1][1] = -height / 2.0f;
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + width / 2.0f;
	result.m[3][1] = top + height / 2.0f;
	result.m[3][2] = minDepth;

	return result;
}