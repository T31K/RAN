

CREATE PROCEDURE [dbo].[sp_delete_guild]    
    @GuNum int,
    @ChaNum  int,	
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
    -- 辨靛沥焊甫 昏力茄促.
    DELETE GuildInfo 
    WHERE GuNum=@GuNum AND ChaNum=@ChaNum
    
    SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
    -- 力措肺 诀单捞飘 登瘤 臼疽阑锭
    IF @error_var <> 0 OR @rowcount_var = 0
    BEGIN
        SET @nReturn = 0
        SET NOCOUNT OFF
        RETURN @nReturn
    END    
    
    -----------------------------------------------------------------
    -- 辨靛俊 啊涝吝牢 葛电 某腐磐狼 辨靛沥焊甫 0 栏肺 技泼茄促.
    UPDATE ChaInfo 
    SET GuNum=0, GuPosition=0 
    WHERE GuNum=@GuNum
    
    -----------------------------------------------------------------
    -- 葛电 悼竿辨靛 沥焊甫 昏力茄促.
    DELETE GuildAlliance 
    WHERE GuNumP=@GuNum OR GuNumS=@GuNum
    
    SET @nReturn = 1
    
    -- 搬苞甫 府畔茄促.
    SET NOCOUNT OFF
    RETURN @nReturn


