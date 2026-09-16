

CREATE PROCEDURE [dbo].[sp_add_guild_member]
    @GuNum int,
    @ChaNum int,
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
    -- Character 甫 秦寸 辨靛糕滚肺 啊涝矫挪促.
    UPDATE ChaInfo 
    SET GuNum=@GuNum 
    WHERE ChaNum=@ChaNum
    
    SELECT @error_var = @@ERROR, @rowcount_var = @@ROWCOUNT
    -- 力措肺 诀单捞飘 登瘤 臼疽阑锭
    IF @error_var <> 0 OR @rowcount_var = 0
    BEGIN
        SET @nReturn = 0
        SET NOCOUNT OFF
        RETURN @nReturn
    END
    ELSE
    BEGIN    
        SET @nReturn = 1
        -- 搬苞甫 府畔茄促.
        SET NOCOUNT OFF
        RETURN @nReturn
    END



