#ifndef __NODEPARSER__
#define __NODEPARSER__

#include <iostream>
#include "cocos2d.h"
#include <string>
#include <stack>
#include <queue>
#if CC_LUA_ENGINE_ENABLED > 0
#include "CCLuaEngine.h"
#endif


using namespace std;
USING_NS_CC;
#define  SEL_IMAGEHANDLER(hander) (ImageNode::Handler)(&hander)
#define  SEL_TEXTHANDLER(handler) (TextNode::Handler)(&handler)


class ImageNode:public CCTouchDelegate,public CCSprite
{    
public:

	typedef void (CCObject::*Handler)(char* id,CCSprite *sprite);
	
	virtual ~ImageNode();

	static ImageNode* create();
	virtual void setAttrs(const char **attrs);
	virtual bool ccTouchBegan(CCTouch *pTouch, CCEvent *pEvent);
	void registerImageHandler(CCObject *obj, Handler handler);
	void registerScriptListener(int listener)
	{
		m_listener=listener;
	}
	void setID(char *id)
	{
		if (this->id!=NULL)
		{
		   delete[] this->id;
		}
		this->id=new char[strlen(id)+1];
		memcpy(this->id,id,strlen(id)+1);
	}
	
	bool getTouchState()
	{
		return touchState;
	}
	void setTouchState(bool state)
	{
		if (touchState==state)
			return;
		else if(state==true)
			CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this,0, true);

		else CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);

		touchState=state;
	}

private:
	ImageNode();
	char * id;
	bool touchState;
	const int getAttrType(const char* attrname);
	CCObject *handleObj;
	Handler  imageHandler;
	int m_listener;
};

class TextNode:public CCTouchDelegate,public CCLabelTTF
{
public:

	typedef void (CCObject::*Handler)(char * id);

	
	virtual ~TextNode();
	static TextNode* create();
	virtual bool ccTouchBegan(CCTouch *pTouch, CCEvent *pEvent);
    void setAttrs(const char **attrs);        
	virtual void draw(void);              //���»���
	void registerTextHandler(CCObject *obj, Handler textHandler);//ע�����ص�����
	void registerScriptListener(int listener)
	{
		m_listener=listener;
	}
	void copyAttrs(TextNode *textNode);  
	void initTextNode(string font,string fontSize,string color);//��ʼ��Ĭ������
	string cutForWidth(float width);               //�ѱ��ڵ��ı����л��� ��width�����ַ���Ϊ���ڵ��ַ�������ʣ���ַ�����
	char* getID()
	{
		return id;
	}
	void setID(char *id)
	{
		if (this->id!=NULL)
		{
			delete[] this->id;
		}
		if (id==NULL)
		{
			return;
		}

		this->id=new char[strlen(id)+1];
		memcpy(this->id,id,strlen(id)+1);
	}
	bool getTouchState()    
	{
		return touchState;
	}   

	//�����Ƿ����¼�����

	void setTouchState(bool state)     
	{
		if (touchState==state)
			return;
		else if(state==true)
		     CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this,0, true);

		else CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);

		touchState=state;
	}   
	CC_SYNTHESIZE(bool,underLineState,UnderLineState);
	CC_SYNTHESIZE(string,lineColor,lineColor);
	CC_SYNTHESIZE(float,lineWidth,LineWidth);
	CC_SYNTHESIZE(bool,strokeState,StrokeState);
	//CC_SYNTHESIZE(int, strokeWidth,StrokeWidth);
	//CC_SYNTHESIZE(string ,strokeColors,StrokeColors);
	//CC_SYNTHESIZE(int, strokeOpacity,StrokeOpacity);
	//CC_SYNTHESIZE(string, strokeDir,StrokeDir);

private:
	TextNode();
	char * id;
	bool touchState;
	const int getAttrType(const char* attrname);
	const ccColor3B getColorFromStr(const char *attr);
	
	bool utf8_isascii(unsigned char c);//c�Ƿ���accii��
	float widthForAscii(char c);//��ȡc�ַ��Ŀ��
	int utf8_char_size(const unsigned char c);//c�ַ��Ĵ�С���ֽ����λΪ1˵����utf8���룬10..zһ���ֽڣ�110..ռ�����ֽڣ�1110...

	CCObject *handleObj;
	Handler  textHandler;
	int m_listener;

	bool updateTexture();
};


class NodeParser : public CCSAXDelegator,public CCObject
{
public:
	
	static NodeParser* create(CCNode *cNode);
	void parseStr(const char *text);                //����xml�ַ���

	//---����xml
	void startElement(void *ctx, const char *name, const char **atts);  
	void endElement(void *ctx, const char *name);
	void textHandler(void *ctx, const char *name, int len);
	//---����xml

	//���в���
    void deploy();    


	          //ע��ص�����
	void registerImageHandler(cocos2d::CCObject *obj, ImageNode::Handler handler);
	void registerTextHandler(cocos2d::CCObject *obj,  TextNode::Handler handler);

	void registerTextScriptListener(int listener)
	{
		textListener=listener;
	}
	void registerImageScriptListener(int listener)
	{
		imageListener=listener;
	}
	virtual ~NodeParser();

	CC_SYNTHESIZE_READONLY(float, height, Height); //��ȡdeploy֮����ռ�߶�
	CC_SYNTHESIZE_READONLY(float, width, Width); //��ȡdeploy֮����ռ���

	CC_SYNTHESIZE(float, verticalMargin, VerticalMargin);
	CC_SYNTHESIZE(float, horizontalMargin, HorizontalMargin);

	CC_SYNTHESIZE_READONLY_PASS_BY_REF(float, parentWidth, ParentWidth);    //�����Ŀ��


	//���ڸ��ֱ�ǩ����ʼ����

	void defaultEnd();
	void trStart(const char **attrs);
	void trEnd();

	void tdStart(const char **attrs);
	void tdEnd();

	void textStart(const char **attrs);
    void textEnd();
	void imgStart(const char **attrs);
	void mStart(const char **attrs);
	void mEnd();

private:
	NodeParser(CCNode *cNode);
	void addToNodes(CCNode* node);
	void updateRowX(float distance);
	void updateRowY(float distance);
	int findNodeType(const char *tagName);
	int findAttrType(const char *name);
	bool isOverOfEdge(CCNode *node);
	void autoWrapText(CCNode *node);
	void setRowAttrs(const char**attrs);
	stack<string> lebalStack;	
	CCNode *parentNode; //��丸�ڵ�
	vector<CCNode*> rowElems;//�����Ѳ��ֵĽڵ�
	deque<CCNode*> rowNodeQueue;//�洢һ�е����нڵ�  
	vector<CCNode*> allNode;//�洢���нڵ�
	CCPoint curRowStart;
	float   curRowHight;
	CCPoint firstRowStart;
	float rowGap;//�м��
	
	//��¼��ǰ�е�����
	string color;
	string font;
	string fontSize;
	string align;


	//ͼƬ�ڵ�ص�
	CCObject *imgHandleObj;
	ImageNode::Handler imgHandle;
	//�ı��ڵ�Ļص�
	CCObject *textHandleObj;
	TextNode::Handler textHandle;

	int textListener;
	int imageListener;
};


enum NodeType
{
	TYPE_NONE = -1,
	TYPE_TR,
	TYPE_TD,
	TYPE_P,
	TYPE_IMG,
	TYPE_END
};

enum NodeAttr
{
	ATTR_ID,
	ATTR_IMG_SRC,
	ATTR_FONT_NAME,
	ATTR_FONT_SIZE,
	ATTR_FONT_COLOR,
	ATTR_ALIGN,
	ATTR_LINECOLOR,
	ATTR_LINEWIDTH,
	ATTR_STROKEWIDTH,
	ATTR_STROKECOLOR,
	ATTR_STROKEOPACITY,
	ATTR_STROKEDIR
};

static const char *attrNames[] =
{
	"id",
	"src",
	"font",
	"size",
	"color",
	"align",
	"lineColor",
	"lineWidth",
	"strokeWidth",
	"strokeColor",
	"strokeOpacity",
	"strokeDir"
};
typedef void (NodeParser::*StartParseFunc)(const char** attrs);
typedef void (NodeParser::*EndParseFunc)();
struct NodeInfo
{
	const char *name;
	StartParseFunc startFunc;
	EndParseFunc endFunc;
};

static NodeInfo nodeInfos[] = {
	{"tr", &NodeParser::trStart, &NodeParser::trEnd},
	{"td", &NodeParser::tdStart, &NodeParser::tdEnd},
	{"p", &NodeParser::textStart, &NodeParser::textEnd},
	{"img", &NodeParser::imgStart, &NodeParser::defaultEnd},
	{"m", &NodeParser::mStart, &NodeParser::mEnd}
};

static const int nodeTypeCount = sizeof(nodeInfos)/sizeof(NodeInfo);
static const int attrCount = sizeof(attrNames)/sizeof(char*);

#endif