import os
import numpy as np
import matplotlib.pyplot as plt
from osgeo import gdal
from numpy.typing import NDArray



BASE_DIR = os.path.dirname(os.path.abspath(__file__))

print(BASE_DIR)
def tif_reader(path):
    """
    读取GeoTIFF文件
    :param path: GeoTIFF数据路径
    :return: 矩阵，地理坐标，坐标系
    """
    if not os.path.exists(path):
        print(f"错误：找不到文件 {path}")
        return None, None, None
    ds = gdal.Open(path)
    if ds is None:
        print(f"错误：无法打开 GeoTIFF 文件 {path}")
        return None, None, None
    data = ds.ReadAsArray()
    ds = gdal.Open(path)
    data = ds.ReadAsArray()
    geo = ds.GetGeoTransform()
    projection =ds.GetProjection()
    ds = None
    return data, geo, projection


src_path = os.path.join(BASE_DIR, 'GeoTIFF_Landmasked','2021_05_30_10_38_06_GF1.tif')
img_time0, _, _ = tif_reader(src_path)  # 读取10:38时刻的卫星图像

src_path = os.path.join(BASE_DIR, 'GeoTIFF_Landmasked','2021_05_30_11_13_47_GF4.tif')
img_time1, _, _ = tif_reader(src_path)  # 读取11:13时刻的卫星图像

# 绘制两时刻卫星图的近红外波段图像
fig, ax = plt.subplots(1, 2, sharex=True, sharey=True)

ax[0].imshow(img_time0[-1])  # 绘制第一时刻图像
ax[1].imshow(img_time1[-1])  # 绘制第二时刻图像

ax[0].axis('off')  # 取消横纵坐标
ax[1].axis('off')  # 取消横纵坐标

plt.show()

def ndvi_calculator(img):
    """
    计算归一化植被指数NDVI
    """
    RED_band = img[-2] # 红波段
    NIR_band = img[-1] # 近红外波段
    
    return (NIR_band - RED_band) / (NIR_band + RED_band)


ndvi_time0 = ndvi_calculator(img_time0)
ndvi_time1 = ndvi_calculator(img_time1)

# 绘制两时刻NDVI图像
fig, ax = plt.subplots(1, 2, sharex=True, sharey=True)

ax[0].imshow(ndvi_time0)  # 绘制第一时刻图像
ax[1].imshow(ndvi_time1)  # 绘制第二时刻图像

ax[0].axis('off')  # 取消横纵坐标
ax[1].axis('off')  # 取消横纵坐标

plt.show()


bloom_time0 = ndvi_time0.copy() 
bloom_time1 = ndvi_time1.copy()

bloom_time0[ndvi_time0 >= 0] = 1   # NDVI >= 0的像素赋值1，也即藻华像素
bloom_time1[ndvi_time1 >= 0] = 1   # NDVI >= 0的像素赋值1，也即藻华像素
bloom_time0[ndvi_time0 < 0] = 0
bloom_time1[ndvi_time1 < 0] = 0

from matplotlib.colors import LinearSegmentedColormap

# 定义颜色映射的色彩分段
colors = [(0.5, 0.7, 1), (0, 0.5, 0)]  # 从浅蓝色到绿色

# 创建颜色映射对象
cmap = LinearSegmentedColormap.from_list('LightBlueToGreen', colors, N=256)
cmap.set_bad(color='white')

# 绘制两时刻藻华像素图像
fig, ax = plt.subplots(1, 2, sharex=True, sharey=True)

ax[0].imshow(bloom_time0, cmap=cmap)  # 绘制第一时刻图像,浅蓝色为水体，绿色为藻华，陆地区域为白色
ax[1].imshow(bloom_time1, cmap=cmap)  # 绘制第二时刻图像,浅蓝色为水体，绿色为藻华，陆地区域为白色

ax[0].axis('off')  # 取消横纵坐标
ax[1].axis('off')  # 取消横纵坐标

plt.show()

def format_img(img0, img1):
    """
    对两幅图像的NDVI归一化，归一化至0-255之间
    """

    img1[np.isnan(img0)] = np.nan  
    img0[np.isnan(img1)] = np.nan
    img0[img0 < -0.2] = -0.2
    img1[img1 < -0.2] = -0.2      # 归一化的最小值定为-0.2
    img0[np.isnan(img0)] = -0.2
    img1[np.isnan(img1)] = -0.2   # nan值设置成最小值

    img0[img0 > 0.6] = 0.6  
    img1[img1 > 0.6] = 0.6   # 归一化的最大值定为0.6
    img0 = (img0 + 0.2) / (0.6 + 0.2)
    img1 = (img1 + 0.2) / (0.6 + 0.2)
    return img0*255, img1*255


prev, curr = format_img(ndvi_time0.copy(), ndvi_time1.copy())  # 归一化至0-255

import cv2


def calculate_dense_optical_flow(prev_img: NDArray[np.floating], curr_img: NDArray[np.floating]) -> NDArray[np.float32]:
    """
    计算两个图像之间的稠密光流。

    参数:
    prev_img (np.array): 第一个时刻的图像。
    curr_img (np.array): 第二个时刻的图像。

    返回:
    np.array: 光流矢量。
    """
    # 将NDVI图像转换为灰度图
    prev_img_gray: NDArray[np.uint8] = np.asarray(prev_img, dtype=np.uint8)
    curr_img_gray: NDArray[np.uint8] = np.asarray(curr_img, dtype=np.uint8)

    # 预分配光流缓存，避免类型检查报错（flow 不能为 None）
    h, w = prev_img_gray.shape
    flow_init: NDArray[np.float32] = np.zeros((h, w, 2), dtype=np.float32)

    # 计算稠密光流
    flow = cv2.calcOpticalFlowFarneback(
        prev=prev_img_gray,
        next=curr_img_gray,
        flow=flow_init,
        pyr_scale=0.5,
        levels=5,
        winsize=80,
        iterations=10,
        poly_n=7,
        poly_sigma=1.5,
        flags=cv2.OPTFLOW_FARNEBACK_GAUSSIAN
    )
    return np.asarray(flow, dtype=np.float32)


# 计算光流，所谓光流，也就是x轴，y轴两个方向上的移动距离
flow = calculate_dense_optical_flow(prev, curr)  # 计算出的光流矩阵维度为(1378, 1120, 2)

flow = flow.transpose(2, 0, 1)  # 把光流矩阵维度调整为(2, 1378, 1120)

flow[-1, :, :] = -flow[-1, :, :]  # opencv库计算出的y轴方向的光流，与实际光流方向相反，所以加个负号转换过来


time_interval = 2141  # 两幅图像的成像时间间隔， 10:38 6秒  到 11:13 47秒    共 2141秒


flow = flow * 50 / time_interval    # 计算藻华像素的漂移速度矢量：漂移的像素 * 空间分辨率（50） /  成像时间间隔


def vector_plot(ndvi0, vector_filter):
    """
    绘制提取出的漂移向量
    """
    h, w = ndvi0.shape
    
    # 创建画图，并对画布进行设置
    fig = plt.figure(figsize=(w/254, h/254))
    fig.tight_layout()
    plt.rcParams["font.sans-serif"] = ["Arial"]
    plt.rcParams["font.size"] = 10
    plt.subplots_adjust(top=1, bottom=0, left=0, right=1, hspace=0, wspace=0)

    # 绘制第一时刻的NDVI图像
    plt.imshow(ndvi0, cmap=cmap)
    
    ##################  绘制漂移矢量   ##############################
    X = np.arange(0, w, 1)
    Y = np.arange(0, h, 1)
    # 每隔30个显示一个，不然太拥挤
    step = 30
    u = vector_filter[0, ::step, ::step]
    v = vector_filter[1, ::step, ::step]
    X = X[::step]
    Y = Y[::step]

    # 只画有效值，把无效值剔除，提高代码运行效率
    indices = np.where(~np.isnan(u))
    X = X[indices[1]]
    Y = Y[indices[0]]
    u = u[indices]
    v = v[indices]
    h1 = plt.quiver(X, Y, u, v,  width=0.003, scale=0.0025, scale_units='xy', headwidth=6, color='red')
    plt.quiverkey(h1, X=0.1, Y=0.125, U=0.3, angle=0, label='Drift Velocity\n0.3 m/s',
                  labelpos='S',          # label在参考箭头的哪个方向; S表示南边
                  color='r',
                  labelcolor='r',   # 箭头颜色 + label的颜色
                  labelsep=0.1,  # 箭头与标签距离
                  alpha=1
                  )
    plt.axis('off')
    plt.show(block=True)
    

vector_plot(ndvi_time0, flow)


bloom_time1 = ndvi_time1.copy()

bloom_time1[ndvi_time1 >= 0] = 1   # NDVI >= 0的像素赋值1，也即藻华像素
bloom_time1[ndvi_time1 < 0] = 0

vector_plot(bloom_time1, flow)

def move_bloom_pixels(bloom_pixels, flow):
    """
    bloom_pixels: 藻华像素矩阵
    flow:         计算出的藻华漂移速度矢量
    return:       根据漂移速度矢量，移动初始藻华像素矩阵，得到位置更新后的像素矩阵
    """
    bloom_pixels_new_position = np.zeros_like(bloom_pixels)  # 创建一个全是0的矩阵
     
    x, y = np.where(bloom_pixels == 1)   # 藻华像素的行列数
    u = flow[0][bloom_pixels == 1]       # 藻华像素在x轴方向上移动的像素数
    v = flow[1][bloom_pixels == 1]       # 藻华像素在y轴方向上移动的像素数
    x_new = x - v   # 更新x轴方向上藻华像素的位置
    y_new = y + u   # 更新y轴方向上藻华像素的位置
    
    # 限制新位置在有效范围内
    x_new = np.clip(x_new, 0, bloom_pixels.shape[0] - 1)
    y_new = np.clip(y_new, 0, bloom_pixels.shape[1] - 1)
    
    x_index = x_new.astype(int)   #  转换数据类型为int
    y_index = y_new.astype(int)   #  转换数据类型为int
    
    bloom_pixels_new_position[x_index, y_index] = 1    # 位置更新后的藻华像素矩阵
    
    bloom_pixels_new_position[np.isnan(bloom_pixels)] = np.nan   # 陆地、岛屿区域赋值为nan
    return bloom_pixels_new_position


xy_offset_1hour = np.round(flow * 1 * 3600 / 50)  # 速度*时间/空间分辨率=移动的像素数
xy_offset_2hour = np.round(flow * 2 * 3600 / 50)  # 速度*时间/空间分辨率=移动的像素数
xy_offset_3hour = np.round(flow * 3 * 3600 / 50)  # 速度*时间/空间分辨率=移动的像素数
xy_offset_4hour = np.round(flow * 4 * 3600 / 50)  # 速度*时间/空间分辨率=移动的像素数

bloom_pixels_1hour = move_bloom_pixels(bloom_time1.copy(), xy_offset_1hour)
bloom_pixels_2hour = move_bloom_pixels(bloom_time1.copy(), xy_offset_2hour)
bloom_pixels_3hour = move_bloom_pixels(bloom_time1.copy(), xy_offset_3hour)
bloom_pixels_4hour = move_bloom_pixels(bloom_time1.copy(), xy_offset_4hour)

fig, ax = plt.subplots(2, 2, sharex=True, sharey=True)
# 去除所有子图的横纵坐标轴
for a in ax.flatten():
    a.set_axis_off()
    
ax[0, 0].imshow(bloom_pixels_1hour, cmap=cmap)
ax[0, 0].set_title('1 hour later')

ax[0, 1].imshow(bloom_pixels_2hour, cmap=cmap)
ax[0, 1].set_title('2 hour later')

ax[1, 0].imshow(bloom_pixels_3hour, cmap=cmap)
ax[1, 0].set_title('3 hour later')

ax[1, 1].imshow(bloom_pixels_4hour, cmap=cmap)
ax[1, 1].set_title('4 hour later')

plt.show()