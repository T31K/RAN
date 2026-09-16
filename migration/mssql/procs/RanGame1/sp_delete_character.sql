


CREATE PROCEDURE [dbo].[sp_delete_character]
	@ChaNum  int,
	@nReturn int OUTPUT
AS    
    DECLARE
	@nGuNum int, -- 辨靛 锅龋    
	@error_var int, 
	@rowcount_var int,
	@nChaDeleted int,
	@nExtreme int


	SET NOCOUNT ON
	
	SET @nReturn = 0
	SET @nGuNum = 0
	SET @error_var = 0
	SET @rowcount_var = 0
	SET @nChaDeleted = 0
	SET @nExtreme = 0
	
	-----------------------------------------------------------------
	-- 捞固 某腐磐啊 昏力登菌绰瘤 炼荤茄促.
	SELECT @nChaDeleted=ChaDeleted FROM ChaInfo WHERE ChaNum=@ChaNum
	IF @nChaDeleted = 1
	BEGIN	
		SET @nReturn = -1
		SET NOCOUNT OFF
		RETURN @nReturn
	END
	
	-----------------------------------------------------------------
    -- 捞固 辨靛 付胶磐肺 涝仿登绢 乐绰瘤 炼荤茄促.  
	SELECT @nGuNum=GuNum FROM GuildInfo WHERE ChaNum=@ChaNum
      
	IF @nGuNum <> 0 -- 沥焊啊 乐澜, 辨靛付胶磐捞扁 锭巩俊 某腐昏力 阂啊/刚历 辨靛沥焊甫 昏力秦具 茄促.
	BEGIN 
		SET @nReturn = -2
		SET NOCOUNT OFF
		RETURN @nReturn
	END

	-----------------------------------------------------------------
	-- 昏力茄 某腐磐啊 必碍何牢瘤 炼荤茄促.
	SELECT @nExtreme=ChaClass From ChaInfo Where ChaNum=@ChaNum
	
	-----------------------------------------------------------------
	-- 某腐磐甫 昏力茄促.
	UPDATE ChaInfo SET ChaDeleted=1 , ChaDeletedDate=getdate() WHERE ChaNum=@ChaNum
    
	IF @nExtreme=16
	BEGIN
		SET @nReturn = 1 -- 必碍何 巢磊啊 昏力登菌促.
	END
	ELSE IF @nExtreme=32
	BEGIN
		SET @nReturn=2 -- 必碍何 咯磊啊 昏力登菌促.
	END
	ELSE
	BEGIN
		SET @nReturn=0
	END
    
	SET NOCOUNT OFF
	RETURN @nReturn


