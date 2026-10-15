#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
OceanLab-Python: 海洋信息实验教学平台
3D渲染模块
"""

import numpy as np
from PyQt5.QtWidgets import QOpenGLWidget
from PyQt5.QtGui import QMatrix4x4, QVector3D, QColor
from PyQt5.QtCore import Qt, QSize
from OpenGL.GL import *
from OpenGL.GLU import *

class PoolRenderer(QOpenGLWidget):
    """水池3D渲染器"""
    
    def __init__(self, parent=None):
        """初始化渲染器"""
        super().__init__(parent)
        
        # 参数
        self.params = {
            'hydrophone_x': 0.0,
            'hydrophone_y': 0.0,
            'hydrophone_z': 0.5,
            'hydrophone_rotation': 0.0,
            'transducer_x': 0.0,
            'transducer_y': 0.0,
            'transducer_z': 0.5,
            'transducer_rotation': 0.0,
            'water_depth': 0.5
        }
        
        # 视角控制
        self.xRot = 30
        self.yRot = -45
        self.zRot = 0
        self.zoom = 5.0
        
        # 鼠标控制
        self.lastPos = None
    
    def minimumSizeHint(self):
        """最小尺寸提示"""
        return QSize(400, 300)
    
    def sizeHint(self):
        """尺寸提示"""
        return QSize(800, 600)
    
    def update_params(self, params):
        """更新参数"""
        for key, value in params.items():
            if key in self.params:
                self.params[key] = value
        
        # 重绘
        self.update()
    
    def initializeGL(self):
        """初始化OpenGL"""
        # 设置清除颜色
        glClearColor(0.1, 0.1, 0.2, 1.0)
        
        # 启用深度测试
        glEnable(GL_DEPTH_TEST)
        
        # 启用光照
        glEnable(GL_LIGHTING)
        glEnable(GL_LIGHT0)
        
        # 设置光源
        glLightfv(GL_LIGHT0, GL_POSITION, [0.0, 0.0, 10.0, 1.0])
        glLightfv(GL_LIGHT0, GL_AMBIENT, [0.2, 0.2, 0.2, 1.0])
        glLightfv(GL_LIGHT0, GL_DIFFUSE, [0.8, 0.8, 0.8, 1.0])
        glLightfv(GL_LIGHT0, GL_SPECULAR, [1.0, 1.0, 1.0, 1.0])
        
        # 启用材质
        glEnable(GL_COLOR_MATERIAL)
        glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE)
    
    def resizeGL(self, width, height):
        """调整OpenGL视口"""
        # 设置视口
        glViewport(0, 0, width, height)
        
        # 设置投影矩阵
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        
        # 透视投影
        aspect = width / height if height > 0 else 1.0
        gluPerspective(45.0, aspect, 0.1, 100.0)
        
        # 切换回模型视图矩阵
        glMatrixMode(GL_MODELVIEW)
    
    def paintGL(self):
        """绘制OpenGL场景"""
        # 清除缓冲区
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        
        # 重置模型视图矩阵
        glLoadIdentity()
        
        # 设置相机位置
        glTranslatef(0.0, 0.0, -self.zoom)
        glRotatef(self.xRot, 1.0, 0.0, 0.0)
        glRotatef(self.yRot, 0.0, 1.0, 0.0)
        glRotatef(self.zRot, 0.0, 0.0, 1.0)
        
        # 绘制水池
        self._draw_pool()
        
        # 绘制水面
        self._draw_water_surface()
        
        # 绘制滑轨
        self._draw_rails()
        
        # 绘制水听器
        self._draw_hydrophone()
        
        # 绘制换能器
        self._draw_transducer()
        
        # 绘制坐标轴
        self._draw_axes()
    
    def _draw_pool(self):
        """绘制水池"""
        # 设置水池颜色
        glColor3f(0.5, 0.5, 0.7)
        
        # 水池尺寸
        pool_length = 2.0  # X轴方向
        pool_width = 1.0   # Y轴方向
        pool_height = 1.0  # Z轴方向
        
        # 绘制水池底部和侧面
        glPushMatrix()
        glTranslatef(0.0, 0.0, 0.0)
        
        # 底部
        glBegin(GL_QUADS)
        glNormal3f(0.0, 0.0, 1.0)
        glVertex3f(-pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, 0.0)
        glVertex3f(-pool_length/2, pool_width/2, 0.0)
        glEnd()
        
        # 前侧面
        glBegin(GL_QUADS)
        glNormal3f(0.0, -1.0, 0.0)
        glVertex3f(-pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, -pool_width/2, pool_height)
        glVertex3f(-pool_length/2, -pool_width/2, pool_height)
        glEnd()
        
        # 后侧面
        glBegin(GL_QUADS)
        glNormal3f(0.0, 1.0, 0.0)
        glVertex3f(-pool_length/2, pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, pool_height)
        glVertex3f(-pool_length/2, pool_width/2, pool_height)
        glEnd()
        
        # 左侧面
        glBegin(GL_QUADS)
        glNormal3f(-1.0, 0.0, 0.0)
        glVertex3f(-pool_length/2, -pool_width/2, 0.0)
        glVertex3f(-pool_length/2, pool_width/2, 0.0)
        glVertex3f(-pool_length/2, pool_width/2, pool_height)
        glVertex3f(-pool_length/2, -pool_width/2, pool_height)
        glEnd()
        
        # 右侧面
        glBegin(GL_QUADS)
        glNormal3f(1.0, 0.0, 0.0)
        glVertex3f(pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, pool_height)
        glVertex3f(pool_length/2, -pool_width/2, pool_height)
        glEnd()
        
        glPopMatrix()
    
    def _draw_water_surface(self):
        """绘制水面"""
        # 获取水深
        water_depth = self.params['water_depth']
        
        # 水池尺寸
        pool_length = 2.0  # X轴方向
        pool_width = 1.0   # Y轴方向
        
        # 设置水面颜色和透明度
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glColor4f(0.2, 0.4, 0.8, 0.5)
        
        # 绘制水面
        glPushMatrix()
        glTranslatef(0.0, 0.0, water_depth)
        
        glBegin(GL_QUADS)
        glNormal3f(0.0, 0.0, 1.0)
        glVertex3f(-pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, -pool_width/2, 0.0)
        glVertex3f(pool_length/2, pool_width/2, 0.0)
        glVertex3f(-pool_length/2, pool_width/2, 0.0)
        glEnd()
        
        glPopMatrix()
        
        glDisable(GL_BLEND)
    
    def _draw_rails(self):
        """绘制滑轨"""
        # 设置滑轨颜色
        glColor3f(0.7, 0.7, 0.7)
        
        # 水池尺寸
        pool_length = 2.0  # X轴方向
        pool_width = 0.5   # Y轴方向
        rail_height = 0.8  # 滑轨高度
        
        # 绘制X轴滑轨
        glPushMatrix()
        glTranslatef(0.0, -pool_width/2, rail_height)
        
        # 绘制圆柱体
        quadric = gluNewQuadric()
        gluCylinder(quadric, 0.02, 0.02, 0.0, 16, 1)
        glRotatef(90, 0.0, 1.0, 0.0)
        gluCylinder(quadric, 0.02, 0.02, pool_length, 16, 1)
        gluDeleteQuadric(quadric)
        
        glPopMatrix()
        
        # 绘制Y轴滑轨
        glPushMatrix()
        glTranslatef(-pool_length/2, 0.0, rail_height)
        
        # 绘制圆柱体
        quadric = gluNewQuadric()
        gluCylinder(quadric, 0.02, 0.02, 0.0, 16, 1)
        glRotatef(90, 1.0, 0.0, 0.0)
        gluCylinder(quadric, 0.02, 0.02, pool_width, 16, 1)
        gluDeleteQuadric(quadric)
        
        glPopMatrix()
    
    def _draw_hydrophone(self):
        """绘制水听器"""
        # 获取水听器参数
        x = self.params['hydrophone_x']
        y = self.params['hydrophone_y']
        z = self.params['hydrophone_z']
        rotation = self.params['hydrophone_rotation']
        
        # 设置水听器颜色
        glColor3f(0.2, 0.8, 0.2)
        
        # 绘制水听器
        glPushMatrix()
        glTranslatef(x, y, z)
        glRotatef(rotation, 0.0, 0.0, 1.0)
        
        # 绘制圆柱体
        quadric = gluNewQuadric()
        gluCylinder(quadric, 0.05, 0.05, 0.15, 16, 1)
        
        # 绘制球体
        glTranslatef(0.0, 0.0, 0.15)
        gluSphere(quadric, 0.05, 16, 16)
        
        gluDeleteQuadric(quadric)
        
        glPopMatrix()
    
    def _draw_transducer(self):
        """绘制换能器"""
        # 获取换能器参数
        x = self.params['transducer_x']
        y = self.params['transducer_y']
        z = self.params['transducer_z']
        rotation = self.params['transducer_rotation']
        
        # 设置换能器颜色
        glColor3f(0.8, 0.2, 0.2)
        
        # 绘制换能器
        glPushMatrix()
        glTranslatef(x, y, z)
        glRotatef(rotation, 0.0, 0.0, 1.0)
        
        # 绘制圆柱体
        quadric = gluNewQuadric()
        gluCylinder(quadric, 0.05, 0.05, 0.15, 16, 1)
        
        # 绘制圆盘
        glTranslatef(0.0, 0.0, 0.15)
        gluDisk(quadric, 0.0, 0.08, 16, 1)
        
        gluDeleteQuadric(quadric)
        
        glPopMatrix()
    
    def _draw_axes(self):
        """绘制坐标轴"""
        # 禁用光照
        glDisable(GL_LIGHTING)
        
        # 绘制坐标轴
        glBegin(GL_LINES)
        
        # X轴 (红色)
        glColor3f(1.0, 0.0, 0.0)
        glVertex3f(0.0, 0.0, 0.0)
        glVertex3f(1.0, 0.0, 0.0)
        
        # Y轴 (绿色)
        glColor3f(0.0, 1.0, 0.0)
        glVertex3f(0.0, 0.0, 0.0)
        glVertex3f(0.0, 1.0, 0.0)
        
        # Z轴 (蓝色)
        glColor3f(0.0, 0.0, 1.0)
        glVertex3f(0.0, 0.0, 0.0)
        glVertex3f(0.0, 0.0, 1.0)
        
        glEnd()
        
        # 重新启用光照
        glEnable(GL_LIGHTING)
    
    def mousePressEvent(self, event):
        """鼠标按下事件"""
        self.lastPos = event.pos()
    
    def mouseMoveEvent(self, event):
        """鼠标移动事件"""
        if self.lastPos is None:
            return
        
        dx = event.x() - self.lastPos.x()
        dy = event.y() - self.lastPos.y()
        
        if event.buttons() & Qt.LeftButton:
            # 旋转视角
            self.yRot += dx
            self.xRot += dy
            self.update()
        elif event.buttons() & Qt.RightButton:
            # 缩放
            self.zoom -= dy / 50.0
            if self.zoom < 1.0:
                self.zoom = 1.0
            elif self.zoom > 20.0:
                self.zoom = 20.0
            self.update()
        
        self.lastPos = event.pos()
    
    def wheelEvent(self, event):
        """鼠标滚轮事件"""
        # 缩放
        delta = event.angleDelta().y() / 120.0
        self.zoom -= delta
        if self.zoom < 1.0:
            self.zoom = 1.0
        elif self.zoom > 20.0:
            self.zoom = 20.0
        self.update() 