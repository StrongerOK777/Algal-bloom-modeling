import os
import numpy as np
from matplotlib.animation import FuncAnimation
import matplotlib.pyplot as plt
import warnings
from osgeo import gdal


BASE_DIR = os.path.dirname(os.path.abspath(__file__))


def tif_reader(path):
    """
    读取GeoTIFF文件
    :param path: GeoTIFF数据路径
    :return: 矩阵，地理坐标，坐标系
    """
    ds = gdal.Open(path)
    if ds is None:
        raise FileNotFoundError(f"无法读取 GeoTIFF 文件: {path}")
    data = ds.ReadAsArray()
    geo = ds.GetGeoTransform()
    projection =ds.GetProjection()
    ds = None
    return data, geo, projection



def truncated_linear_stretch(gray, truncated_value=0.3):
    """
    图像拉伸
    :param gray: 二维矩阵，不可是三维
    :param truncated_value: 拉伸值
    :return: 拉伸后的矩阵，值域为[0, 1]
    """
    nan_flag = np.isnan(gray)
    gray_flatten = gray[nan_flag == False]
    if len(gray_flatten) >= 5:
        truncated_down = np.percentile(gray_flatten, truncated_value)
        truncated_up = np.percentile(gray_flatten, 100 - truncated_value)
        gray = (gray - truncated_down) / (truncated_up - truncated_down)
        gray[gray < 0] = 0
        gray[gray > 1] = 1
    else:
        warnings.warn('矩阵有效值太少，未作拉伸，返回结果为原矩阵')
    return gray


def rgb_calculator(img, n=0):
    """
    利用GeoTIFF 4波段图像，提取RGB图像
    """
    if n == 0:
        per = 2
    else:
        per = 4
    r = truncated_linear_stretch(img[-2], per)
    g = truncated_linear_stretch(img[-3], per)
    b = truncated_linear_stretch(img[-4], per)
    rgb = np.array([r, g, b])
    # 左上角背景设置为黑色，方便添加时间标签
    rgb[:, :106, :450] = 0
    return rgb


def gif_plot(rgb, time_label, ax):
    # 绘图
    ax.imshow(rgb.transpose(1, 2, 0))
    # 添加时间标签
    ax.text(5, 100, time_label, c='w', weight='bold')


img_path0 = os.path.join(BASE_DIR, 'GeoTIFF', '2021_05_30_10_38_06_GF1.tif')
img_path1 = os.path.join(BASE_DIR, 'GeoTIFF', '2021_05_30_11_13_47_GF4.tif')
img0, _, _ = tif_reader(img_path0)
img1, _, _ = tif_reader(img_path1)
rgb0 = rgb_calculator(img0)
rgb1 = rgb_calculator(img1, n=1)

_, h, w = rgb0.shape

rgbs = [rgb0, rgb1]
time_labels = ['2021-05-31T10:38\nGaofen-1', '2021-05-31T11:13\nGaofen-4']

fig, ax = plt.subplots(1, 1, figsize=(w/254, h/254))
plt.rcParams["font.sans-serif"] = ["Arial"]
plt.rcParams["font.size"] = 15
plt.subplots_adjust(top=1, bottom=0, left=0, right=1)


def update(n):
    ax.cla()
    gif_plot(rgbs[n], time_labels[n], ax)
    return [*ax.images, *ax.texts]


ani = FuncAnimation(fig, update, frames=2, interval=600)
dst = os.path.join(BASE_DIR, '卫星动态图.gif')
ani.save(dst, writer='imagemagick', dpi=800)
plt.show()