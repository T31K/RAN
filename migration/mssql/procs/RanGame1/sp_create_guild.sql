

CREATE PROCEDURE [dbo].[sp_create_guild]
    @ChaNum  int,
	@GuName  char(33),
	@nReturn int	OUTPUT
AS    
    DECLARE @nGuNum int, -- 辨靛 锅龋         
         @error_var int, 
		 @rowcount_var int

	SET NOCOUNT ON
	
	SET @nReturn = 0
	SET	@nGuNum = 0
	SET @error_var = 0
	SET @rowcount_var = 0
	
    -----------------------------------------------------------------
    -- 捞固 辨靛 付胶磐肺 涝仿登绢 乐绰瘤 炼荤茄促.  
	SELECT @nGuNum=GuNum 
	FROM GuildInfo 
	WHERE ChaNum=@ChaNum
      
	IF @nGuNum <> 0 -- 沥焊啊 乐澜, 辨靛积己阂啊瓷
	BEGIN 
		SET @nReturn = -1
		SET NOCOUNT OFF
		RETURN @nReturn
	END
	
	-----------------------------------------------------------------
    -- 货肺款 辨靛甫 积己茄促.
    INSERT INTO GuildInfo (ChaNum, GuName) 
    VALUES (@ChaNum, @GuName)
    
    SELECT @error_var = @@ERROR
    IF @error_var <> 0 
    BEGIN
        -- 辨靛 积己吝 俊矾惯积
        SET @nReturn = -2
        SET NOCOUNT OFF
        RETURN @nReturn
    END
    
    -----------------------------------------------------------------
    -- 货肺款 辨靛 积己己傍, 辨靛锅龋甫 啊廉柯促. 
    SELECT @nGuNum=@@IDENTITY
    
    SET @nReturn = @nGuNum
    
    -----------------------------------------------------------------
    -- 辨靛甫 积己茄 某腐磐(Guild Master)狼 某腐磐沥焊俊 辨靛沥焊甫 涝仿茄促.
    UPDATE ChaInfo 
    SET GuNum=@nGuNum 
    WHERE ChaNum=@ChaNum
    
    -- 积己等 辨靛锅龋甫 府畔茄促.
    SET NOCOUNT OFF
    RETURN @nReturn


